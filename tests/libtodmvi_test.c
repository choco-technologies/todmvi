#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "libtodmvi.h"
#include <errno.h>
#include <string.h>

/*
 * libtodmvi: pixels and "TRAW" image files (the test decoder dmimg_traw,
 * traw/) converted into .dmvi files, read back and checked against
 * dmview's docs/image-format.md.
 */

#ifndef LIBTODMVI_TEST_DIR
#define LIBTODMVI_TEST_DIR "."
#endif
#define OUTPUT(name)    LIBTODMVI_TEST_DIR "/" name

#define HEADER  ((uint32_t)sizeof(dmvi_header_t))
#define MAX_W   64
#define MAX_H   64

static uint32_t g_px[MAX_W * MAX_H];
static uint8_t  g_file[MAX_W * MAX_H * 4 + 2048];   /* The .dmvi, unpacked */
static uint32_t g_file_size;                        /* As written */
static dmvi_header_t g_h;

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) { return (uint32_t)rd16(p) | ((uint32_t)rd16(p + 2) << 16); }

/* Read a .dmvi back into g_file - unpacked - and g_h */
static bool load(const char* path)
{
    static uint8_t raw[sizeof(g_file)];
    void* f = Dmod_FileOpen(path, "rb");
    if (f == NULL)
        return false;
    g_file_size = (uint32_t)Dmod_FileRead(raw, 1, sizeof(raw), f);
    Dmod_FileClose(f);
    if (g_file_size < HEADER || memcmp(raw, "DMVI", 4) != 0 || rd32(raw + 8) != g_file_size)
        return false;
    memcpy(&g_h, raw, HEADER);
    memcpy(g_file, raw, HEADER);
    if (g_h.compression[0] == '\0')
    {
        memcpy(g_file + HEADER, raw + HEADER, g_file_size - HEADER);
        return g_h.unpacked_size == g_file_size - HEADER;
    }
    return Dmod_Compression_Unpack(g_h.compression, g_file + HEADER, sizeof(g_file) - HEADER, raw + HEADER,
                                   g_file_size - HEADER) == g_h.unpacked_size;
}

static const uint8_t* row(uint32_t y) { return g_file + g_h.pixels + y * g_h.stride; }

static uint32_t pattern(uint32_t x, uint32_t y)
{
    return 0xFF000000u | ((x * 4u) << 16) | ((y * 4u) << 8) | ((x * y) & 0xFFu);
}

static libtodmvi_options_t uncompressed(uint8_t format)
{
    libtodmvi_options_t o;
    memset(&o, 0, sizeof(o));
    o.format = format;
    o.compression = "";
    return o;
}

DMOD_TEST_STEP(libtodmvi_chooses_the_format)
{
    libtodmvi_result_t r;
    libtodmvi_options_t o = uncompressed(LIBTODMVI_FORMAT_AUTO);

    /* Many colors, opaque: RGB565 */
    for (uint32_t i = 0; i < 32 * 32; i++)
        g_px[i] = pattern(i % 32, i / 32);
    DMOD_TEST_EXPECT_EQ(libtodmvi_convert_pixels(g_px, 32, 32, 32, OUTPUT("a.dmvi"), &o, &r), 0);
    DMOD_TEST_EXPECT_EQ(r.format, DMVI_FORMAT_RGB565);
    DMOD_TEST_EXPECT_EQ(r.colors, 257u);

    /* Translucent: RGB565A8 */
    g_px[5] = 0x80FFFFFFu;
    DMOD_TEST_EXPECT_EQ(libtodmvi_convert_pixels(g_px, 32, 32, 32, OUTPUT("a.dmvi"), &o, &r), 0);
    DMOD_TEST_EXPECT_EQ(r.format, DMVI_FORMAT_RGB565A8);

    /* Up to 256 colors: I8 */
    for (uint32_t i = 0; i < 32 * 32; i++)
        g_px[i] = (i % 3 == 0) ? 0x00123456u : 0xFF000000u | (i % 7u);    /* transparent: all one */
    DMOD_TEST_EXPECT_EQ(libtodmvi_convert_pixels(g_px, 32, 32, 32, OUTPUT("a.dmvi"), &o, &r), 0);
    DMOD_TEST_EXPECT_EQ(r.format, DMVI_FORMAT_I8);
    DMOD_TEST_EXPECT_EQ(r.colors, 8u);
    DMOD_TEST_EXPECT_TRUE(load(OUTPUT("a.dmvi")));
    DMOD_TEST_EXPECT_EQ(g_h.palette_count, 8u);
    DMOD_TEST_EXPECT_EQ(rd32(g_file + g_h.palette + row(0)[0] * 4u), 0x00000000u);
    DMOD_TEST_EXPECT_EQ(rd32(g_file + g_h.palette + row(0)[1] * 4u), 0xFF000001u);
    DMOD_TEST_EXPECT_EQ(rd32(g_file + g_h.palette + row(1)[0] * 4u), 0xFF000004u);     /* pixel 32 */

    /* I8 is asked for, but there are more colors */
    for (uint32_t i = 0; i < 32 * 32; i++)
        g_px[i] = pattern(i % 32, i / 32);
    o.format = DMVI_FORMAT_I8;
    DMOD_TEST_EXPECT_EQ(libtodmvi_convert_pixels(g_px, 32, 32, 32, OUTPUT("a.dmvi"), &o, &r), -ENOTSUP);
}

DMOD_TEST_STEP(libtodmvi_writes_every_format)
{
    libtodmvi_options_t o;
    for (uint32_t i = 0; i < 5 * 3; i++)
        g_px[i] = 0x80000000u | ((i % 5) * 0x10203u);
    g_px[0] = 0xFFFF0000u;
    g_px[1] = 0x00000000u;

    o = uncompressed(DMVI_FORMAT_ARGB8888);
    DMOD_TEST_EXPECT_EQ(libtodmvi_convert_pixels(g_px, 5, 3, 5, OUTPUT("b.dmvi"), &o, NULL), 0);
    DMOD_TEST_EXPECT_TRUE(load(OUTPUT("b.dmvi")));
    DMOD_TEST_EXPECT_EQ(g_h.width, 5u);
    DMOD_TEST_EXPECT_EQ(g_h.height, 3u);
    DMOD_TEST_EXPECT_EQ(g_h.stride, 20u);
    DMOD_TEST_EXPECT_EQ(g_h.pixels, HEADER);
    DMOD_TEST_EXPECT_EQ(rd32(row(0)), 0xFFFF0000u);
    DMOD_TEST_EXPECT_EQ(rd32(row(2) + 4 * 4), g_px[14]);

    o = uncompressed(DMVI_FORMAT_RGB565A8);
    o.no_dither = true;
    DMOD_TEST_EXPECT_EQ(libtodmvi_convert_pixels(g_px, 5, 3, 5, OUTPUT("b.dmvi"), &o, NULL), 0);
    DMOD_TEST_EXPECT_TRUE(load(OUTPUT("b.dmvi")));
    DMOD_TEST_EXPECT_EQ(g_h.stride, 12u);                       /* 10, 4-aligned */
    DMOD_TEST_EXPECT_EQ(g_h.alpha_stride, 8u);
    DMOD_TEST_EXPECT_EQ(g_h.alpha, HEADER + 3u * 12u);
    DMOD_TEST_EXPECT_EQ(rd16(row(0)), 0xF800u);
    DMOD_TEST_EXPECT_EQ(g_file[g_h.alpha], 0xFFu);
    DMOD_TEST_EXPECT_EQ(g_file[g_h.alpha + 1], 0u);
    DMOD_TEST_EXPECT_EQ(g_file[g_h.alpha + 8 + 2], 0x80u);
    DMOD_TEST_EXPECT_EQ(g_h.file_size, HEADER + g_h.unpacked_size);

    o = uncompressed(DMVI_FORMAT_A8);
    DMOD_TEST_EXPECT_EQ(libtodmvi_convert_pixels(g_px, 5, 3, 5, OUTPUT("b.dmvi"), &o, NULL), 0);
    DMOD_TEST_EXPECT_TRUE(load(OUTPUT("b.dmvi")));
    DMOD_TEST_EXPECT_EQ(row(0)[0], 0xFFu);                      /* the alpha */
    DMOD_TEST_EXPECT_EQ(row(0)[1], 0u);
    DMOD_TEST_EXPECT_EQ(row(1)[3], 0x80u);

    o = uncompressed(DMVI_FORMAT_A4);
    DMOD_TEST_EXPECT_EQ(libtodmvi_convert_pixels(g_px, 5, 3, 5, OUTPUT("b.dmvi"), &o, NULL), 0);
    DMOD_TEST_EXPECT_TRUE(load(OUTPUT("b.dmvi")));
    DMOD_TEST_EXPECT_EQ(g_h.stride, 4u);                        /* 3, 4-aligned */
    DMOD_TEST_EXPECT_EQ(row(0)[0], 0x0Fu);                      /* 15 | 0 << 4 */
    DMOD_TEST_EXPECT_EQ(row(0)[1], 0x88u);                      /* 128 -> 8, 8 */

    /* An opaque image as a mask: its brightness */
    g_px[0] = 0xFFFFFFFFu;
    g_px[1] = 0xFF000000u;
    o = uncompressed(DMVI_FORMAT_A8);
    DMOD_TEST_EXPECT_EQ(libtodmvi_convert_pixels(g_px, 2, 1, 2, OUTPUT("b.dmvi"), &o, NULL), 0);
    DMOD_TEST_EXPECT_TRUE(load(OUTPUT("b.dmvi")));
    DMOD_TEST_EXPECT_EQ(row(0)[0], 0xFFu);
    DMOD_TEST_EXPECT_EQ(row(0)[1], 0u);
}

DMOD_TEST_STEP(libtodmvi_dithers_rgb565)
{
    libtodmvi_options_t o = uncompressed(DMVI_FORMAT_RGB565);
    for (uint32_t i = 0; i < 8 * 8; i++)
        g_px[i] = 0xFF000000u | (0x86u << 16);                  /* red: 16.29 levels of 5 bits */

    DMOD_TEST_EXPECT_EQ(libtodmvi_convert_pixels(g_px, 8, 8, 8, OUTPUT("c.dmvi"), &o, NULL), 0);
    DMOD_TEST_EXPECT_TRUE(load(OUTPUT("c.dmvi")));
    uint32_t low = 0, high = 0;
    for (uint32_t y = 0; y < 8; y++)
        for (uint32_t x = 0; x < 8; x++)
        {
            uint16_t p = rd16(row(y) + x * 2);
            low += p == (16u << 11);
            high += p == (17u << 11);
        }
    DMOD_TEST_EXPECT_EQ(low + high, 64u);
    DMOD_TEST_EXPECT_EQ(high, 16u);                             /* 4 of the 16 thresholds round up */

    o.no_dither = true;
    DMOD_TEST_EXPECT_EQ(libtodmvi_convert_pixels(g_px, 8, 8, 8, OUTPUT("c.dmvi"), &o, NULL), 0);
    DMOD_TEST_EXPECT_TRUE(load(OUTPUT("c.dmvi")));
    DMOD_TEST_EXPECT_EQ(rd16(row(3) + 6), 16u << 11);
    DMOD_TEST_EXPECT_EQ(rd16(row(0)), 16u << 11);
}

DMOD_TEST_STEP(libtodmvi_scales_down)
{
    libtodmvi_result_t r;
    libtodmvi_options_t o = uncompressed(DMVI_FORMAT_ARGB8888);
    for (uint32_t i = 0; i < 8 * 4; i++)
        g_px[i] = ((i % 8) < 4) ? 0xFF000000u : 0xFFFFFFFFu;    /* black | white */
    g_px[0] = 0x00000000u;                                      /* a transparent corner */

    o.max_width = 4;
    o.max_height = 4;                                           /* 8x4 -> 4x2 */
    DMOD_TEST_EXPECT_EQ(libtodmvi_convert_pixels(g_px, 8, 4, 8, OUTPUT("d.dmvi"), &o, &r), 0);
    DMOD_TEST_EXPECT_EQ(r.width, 4u);
    DMOD_TEST_EXPECT_EQ(r.height, 2u);
    DMOD_TEST_EXPECT_TRUE(load(OUTPUT("d.dmvi")));
    DMOD_TEST_EXPECT_EQ(rd32(row(0)), 0xBF000000u);             /* 3 of 4 opaque, still black */
    DMOD_TEST_EXPECT_EQ(rd32(row(0) + 12), 0xFFFFFFFFu);
    DMOD_TEST_EXPECT_EQ(rd32(row(1)), 0xFF000000u);

    /* One side only; never larger */
    o.max_width = 0;
    o.max_height = 1;
    DMOD_TEST_EXPECT_EQ(libtodmvi_convert_pixels(g_px, 8, 4, 8, OUTPUT("d.dmvi"), &o, &r), 0);
    DMOD_TEST_EXPECT_TRUE(r.width == 2u && r.height == 1u);
    o.max_width = 100;
    o.max_height = 100;
    DMOD_TEST_EXPECT_EQ(libtodmvi_convert_pixels(g_px, 8, 4, 8, OUTPUT("d.dmvi"), &o, &r), 0);
    DMOD_TEST_EXPECT_TRUE(r.width == 8u && r.height == 4u);
}

/* xorshift - pixels that do not compress */
static uint32_t g_seed = 12345u;
static uint32_t noise(void)
{
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}

DMOD_TEST_STEP(libtodmvi_compresses_when_it_helps)
{
    libtodmvi_result_t r;
    libtodmvi_options_t o;
    memset(&o, 0, sizeof(o));
    o.format = DMVI_FORMAT_ARGB8888;
    if (!Dmod_Compression_IsSupported("fastlz"))
    {
        Dmod_Printf("    no fastlz in this dmod - skipped\n");
        return;
    }

    for (uint32_t i = 0; i < 64 * 64; i++)
        g_px[i] = 0xFF2040A0u;                                  /* flat */
    DMOD_TEST_EXPECT_EQ(libtodmvi_convert_pixels(g_px, 64, 64, 64, OUTPUT("e.dmvi"), &o, &r), 0);
    DMOD_TEST_EXPECT_TRUE(r.compressed);
    DMOD_TEST_EXPECT_TRUE(r.size < r.unpacked_size / 8u);
    DMOD_TEST_EXPECT_TRUE(load(OUTPUT("e.dmvi")));
    DMOD_TEST_EXPECT_TRUE(strcmp(g_h.compression, "fastlz") == 0);
    DMOD_TEST_EXPECT_EQ(rd32(row(63) + 63 * 4), 0xFF2040A0u);

    for (uint32_t i = 0; i < 64 * 64; i++)
        g_px[i] = noise() | 0xFF000000u;
    DMOD_TEST_EXPECT_EQ(libtodmvi_convert_pixels(g_px, 64, 64, 64, OUTPUT("e.dmvi"), &o, &r), 0);
    DMOD_TEST_EXPECT_FALSE(r.compressed);                       /* it would not be smaller */
    DMOD_TEST_EXPECT_TRUE(load(OUTPUT("e.dmvi")));
    DMOD_TEST_EXPECT_EQ(g_h.compression[0], '\0');
    DMOD_TEST_EXPECT_EQ(rd32(row(10) + 7 * 4), g_px[10 * 64 + 7]);

    o.compression = "nolz";
    DMOD_TEST_EXPECT_EQ(libtodmvi_convert_pixels(g_px, 64, 64, 64, OUTPUT("e.dmvi"), &o, &r), -ENOTSUP);
}

/* A TRAW file (traw/): magic, uint16_t width, height, 0xAARRGGBB pixels */
static bool write_traw(const char* path, uint32_t w, uint32_t h)
{
    static uint8_t data[8 + MAX_W * MAX_H * 4];
    memcpy(data, "TRAW", 4);
    data[4] = (uint8_t)w;
    data[5] = 0;
    data[6] = (uint8_t)h;
    data[7] = 0;
    for (uint32_t i = 0; i < w * h; i++)
    {
        uint32_t p = pattern(i % w, i / w);
        memcpy(data + 8 + i * 4, &p, 4);
    }
    void* f = Dmod_FileOpen(path, "wb");
    if (f == NULL)
        return false;
    bool ok = Dmod_FileWrite(data, 1, 8 + w * h * 4, f) == 8 + w * h * 4;
    Dmod_FileClose(f);
    return ok;
}

DMOD_TEST_STEP(libtodmvi_converts_image_files)
{
    libtodmvi_result_t r;
    libtodmvi_options_t o = uncompressed(DMVI_FORMAT_ARGB8888);
    DMOD_TEST_EXPECT_TRUE(write_traw(OUTPUT("f.traw"), 16, 8));

    DMOD_TEST_EXPECT_EQ(libtodmvi_convert_file(OUTPUT("f.traw"), OUTPUT("f.dmvi"), &o, &r), 0);
    DMOD_TEST_EXPECT_TRUE(strcmp(r.decoder, "dmimg_traw") == 0);
    DMOD_TEST_EXPECT_TRUE(r.source_width == 16u && r.width == 16u && r.height == 8u);
    DMOD_TEST_EXPECT_TRUE(load(OUTPUT("f.dmvi")));
    DMOD_TEST_EXPECT_EQ(rd32(row(5) + 9 * 4), pattern(9, 5));

    /* Half the size: decoded at 1/2 - every second pixel, as dmimg_traw does */
    o.max_width = 8;
    DMOD_TEST_EXPECT_EQ(libtodmvi_convert_file(OUTPUT("f.traw"), OUTPUT("f.dmvi"), &o, &r), 0);
    DMOD_TEST_EXPECT_TRUE(r.width == 8u && r.height == 4u);
    DMOD_TEST_EXPECT_TRUE(load(OUTPUT("f.dmvi")));
    DMOD_TEST_EXPECT_EQ(rd32(row(1) + 3 * 4), pattern(6, 2));

    DMOD_TEST_EXPECT_EQ(libtodmvi_convert_file(OUTPUT("missing.traw"), OUTPUT("g.dmvi"), &o, &r), -ENOENT);
    DMOD_TEST_EXPECT_FALSE(Dmod_FileAvailable(OUTPUT("g.dmvi")));
}

DMOD_TEST_STEP(libtodmvi_names_formats)
{
    DMOD_TEST_EXPECT_EQ(libtodmvi_format_by_name("rgb565a8"), DMVI_FORMAT_RGB565A8);
    DMOD_TEST_EXPECT_EQ(libtodmvi_format_by_name("auto"), (int)LIBTODMVI_FORMAT_AUTO);
    DMOD_TEST_EXPECT_EQ(libtodmvi_format_by_name("png"), -1);
    DMOD_TEST_EXPECT_TRUE(strcmp(libtodmvi_format_name(DMVI_FORMAT_A4), "a4") == 0);
    DMOD_TEST_EXPECT_TRUE(libtodmvi_format_name(99) == NULL);
}
