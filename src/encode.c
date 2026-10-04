#include "private.h"
#include <errno.h>
#include <string.h>

/*
 * Encoding: the pixels in the chosen .dmvi format, laid out as dmview's
 * docs/image-format.md says (rows 4-aligned), packed with dmod's
 * compression when that makes the file smaller, written to a temporary
 * file and renamed.
 */

#define HEADER          ((uint32_t)sizeof(dmvi_header_t))
#define HASH_SIZE       512u            /* Open addressing for up to DMVI_PALETTE_MAX colors */
#define AUTO_COMPRESSION "fastlz"

static inline uint32_t align4(uint32_t n) { return (n + 3U) & ~3U; }

static void wr16(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t* p, uint32_t v) { wr16(p, v); wr16(p + 2, v >> 16); }

/* A fully transparent pixel has no color: they are all one */
static inline uint32_t normal(uint32_t c) { return ((c >> 24) == 0) ? 0 : c; }

/* ---- Colors ---- */

typedef struct
{
    uint32_t    key[HASH_SIZE];
    uint16_t    index[HASH_SIZE];
    bool        used[HASH_SIZE];
    uint32_t    count;
    uint32_t    colors[DMVI_PALETTE_MAX];
} palette_t;

/* Index of color `c`, added when it is new; -1 when the palette is full */
static int palette_index(palette_t* p, uint32_t c)
{
    uint32_t h = (c * 2654435761u) >> 23;          /* 9 bits */
    while (p->used[h])
    {
        if (p->key[h] == c)
            return p->index[h];
        h = (h + 1U) % HASH_SIZE;
    }
    if (p->count == DMVI_PALETTE_MAX)
        return -1;
    p->used[h] = true;
    p->key[h] = c;
    p->index[h] = (uint16_t)p->count;
    p->colors[p->count] = c;
    return (int)p->count++;
}

/* Whether every pixel is opaque; the colors, up to DMVI_PALETTE_MAX + 1 */
static bool analyze(const uint32_t* px, uint32_t w, uint32_t h, uint32_t stride, palette_t* p, uint32_t* colors)
{
    bool opaque = true, full = false;
    for (uint32_t y = 0; y < h; y++)
    {
        for (uint32_t x = 0; x < w; x++)
        {
            uint32_t c = normal(px[y * stride + x]);
            opaque = opaque && (c >> 24) == 0xFFu;
            if (!full && palette_index(p, c) < 0)
                full = true;
        }
    }
    *colors = p->count + (full ? 1U : 0U);
    return opaque;
}

/* ---- Pixels ---- */

static const uint8_t g_bayer[4][4] = {
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 },
};

/* RGB565 of `c` at x, y - the threshold as libdmview's dithered gradients,
 * or rounded */
static uint16_t to_rgb565(uint32_t c, uint32_t x, uint32_t y, bool dither)
{
    uint32_t t = dither ? ((uint32_t)g_bayer[y & 3U][x & 3U] * 255U + 8U) / 16U : 127U;
    uint32_t r = (((c >> 16) & 0xFFu) * 31U + t) / 255U;
    uint32_t g = (((c >> 8) & 0xFFu) * 63U + t) / 255U;
    uint32_t b = ((c & 0xFFu) * 31U + t) / 255U;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* Coverage of a mask: the alpha - or, for an opaque image, the brightness */
static inline uint32_t coverage(uint32_t c, bool opaque)
{
    if (!opaque)
        return c >> 24;
    return ((((c >> 16) & 0xFFu) * 77U + ((c >> 8) & 0xFFu) * 150U + (c & 0xFFu) * 29U) + 128U) >> 8;
}

typedef struct
{
    uint32_t    stride, pixels;
    uint32_t    alpha_stride, alpha;
    uint32_t    palette;
    uint32_t    end;                /* Offset of the end of the unpacked image */
} layout_t;

static bool lay_out(uint8_t format, uint32_t w, uint32_t h, uint32_t colors, layout_t* l)
{
    uint64_t row;
    switch (format)
    {
        case DMVI_FORMAT_RGB565:
        case DMVI_FORMAT_RGB565A8:  row = (uint64_t)w * 2U; break;
        case DMVI_FORMAT_ARGB8888:  row = (uint64_t)w * 4U; break;
        case DMVI_FORMAT_A4:        row = ((uint64_t)w + 1U) / 2U; break;
        default:                    row = w; break;
    }
    memset(l, 0, sizeof(*l));
    l->stride = align4((uint32_t)row);
    l->pixels = HEADER;
    uint64_t end = (uint64_t)HEADER + (uint64_t)l->stride * h;
    if (format == DMVI_FORMAT_RGB565A8)
    {
        l->alpha_stride = align4(w);
        l->alpha = (uint32_t)end;
        end += (uint64_t)l->alpha_stride * h;
    }
    if (format == DMVI_FORMAT_I8)
    {
        l->palette = (uint32_t)end;
        end += (uint64_t)colors * 4U;
    }
    if (end > 0x7FFFFFFFu)
        return false;
    l->end = (uint32_t)end;
    return true;
}

/* The unpacked image after the header, into `data` (zeroed, l->end bytes) */
static void fill(uint8_t* data, const layout_t* l, uint8_t format, const uint32_t* px, uint32_t w, uint32_t h,
                 uint32_t stride, bool opaque, palette_t* p, bool dither)
{
    for (uint32_t y = 0; y < h; y++)
    {
        uint8_t* row = data + l->pixels + y * l->stride;
        uint8_t* alpha = data + l->alpha + y * l->alpha_stride;
        for (uint32_t x = 0; x < w; x++)
        {
            uint32_t c = normal(px[y * stride + x]);
            switch (format)
            {
                case DMVI_FORMAT_RGB565:
                    wr16(row + x * 2U, to_rgb565(c, x, y, dither));
                    break;
                case DMVI_FORMAT_RGB565A8:
                    wr16(row + x * 2U, ((c >> 24) == 0) ? 0 : to_rgb565(c, x, y, dither));
                    alpha[x] = (uint8_t)(c >> 24);
                    break;
                case DMVI_FORMAT_ARGB8888:
                    wr32(row + x * 4U, c);
                    break;
                case DMVI_FORMAT_I8:
                    row[x] = (uint8_t)palette_index(p, c);
                    break;
                case DMVI_FORMAT_A8:
                    row[x] = (uint8_t)coverage(c, opaque);
                    break;
                default:            /* A4: the left pixel in the low nibble */
                    row[x / 2U] |= (uint8_t)(((coverage(c, opaque) * 15U + 127U) / 255U) << ((x & 1U) * 4U));
                    break;
            }
        }
    }
    if (format == DMVI_FORMAT_I8)
    {
        for (uint32_t i = 0; i < p->count; i++)
            wr32(data + l->palette + i * 4U, p->colors[i]);
    }
}

/* ---- Compression ---- */

/* The compression to use: NULL for none, or -ENOTSUP in *status */
static const char* compression_of(const libtodmvi_options_t* o, int* status)
{
    *status = 0;
    if (o->compression == NULL)
        return Dmod_Compression_IsSupported(AUTO_COMPRESSION) ? AUTO_COMPRESSION : NULL;
    if (o->compression[0] == '\0')
        return NULL;
    if (strlen(o->compression) >= DMVI_COMPRESSION_SIZE || !Dmod_Compression_IsSupported(o->compression))
    {
        *status = -ENOTSUP;
        return NULL;
    }
    return o->compression;
}

/* Pack what follows the header; the packed bytes (allocated) and their
 * size, NULL to store it as it is */
static uint8_t* pack(const char* name, bool must, const uint8_t* data, uint32_t size, uint32_t* packed, int* status)
{
    *status = 0;
    size_t max = Dmod_Compression_GetMaxSize(name, LIBTODMVI_COMPRESSION_LEVEL, size);
    uint8_t* out = (max != 0) ? Dmod_Malloc(max) : NULL;
    if (out == NULL)
    {
        *status = (max != 0) ? -ENOMEM : -ENOTSUP;
        return NULL;
    }
    size_t n = Dmod_Compression_Pack(name, LIBTODMVI_COMPRESSION_LEVEL, out, max, data, size);
    if (n == 0 || (!must && n >= size))
    {
        Dmod_Free(out);
        if (n == 0 && must)
            *status = -EIO;
        return NULL;
    }
    *packed = (uint32_t)n;
    return out;
}

/* ---- The file ---- */

/* "<output>.<pid>-<id>.tmp" (allocated), unique for every running
 * conversion: the pid, and the address of a local of the call */
static char* temp_path(const char* output, const void* unique)
{
    size_t size = strlen(output) + 32U;
    char* path = Dmod_Malloc(size);
    if (path != NULL)
        Dmod_SnPrintf(path, size, "%s.%x-%x.tmp", output, (unsigned)Dmod_GetCurrentPid(), (unsigned)(uintptr_t)unique);
    return path;
}

static int write_file(const char* output, const uint8_t* header, const uint8_t* body, uint32_t size)
{
    char* temp = temp_path(output, &header);
    if (temp == NULL)
        return -ENOMEM;
    void* f = Dmod_FileOpen(temp, "wb");
    if (f == NULL)
    {
        Dmod_Free(temp);
        return -EIO;
    }
    bool ok = Dmod_FileWrite(header, 1, HEADER, f) == HEADER && Dmod_FileWrite(body, 1, size, f) == size;
    Dmod_FileClose(f);

    /* The old output first: not every file system's rename replaces a file */
    if (ok && Dmod_FileAvailable(output))
        (void)Dmod_FileRemove(output);
    ok = ok && Dmod_Rename(temp, output) == 0;
    if (!ok)
        (void)Dmod_FileRemove(temp);
    Dmod_Free(temp);
    return ok ? 0 : -EIO;
}

int encode_file(const uint32_t* px, uint32_t w, uint32_t h, uint32_t stride, const char* output,
                const libtodmvi_options_t* o, libtodmvi_result_t* result)
{
    if (w > 0xFFFFu || h > 0xFFFFu)
        return -E2BIG;
    int status = 0;
    const char* compression = compression_of(o, &status);
    if (status != 0)
        return status;

    palette_t* p = Dmod_Malloc(sizeof(*p));
    if (p == NULL)
        return -ENOMEM;
    memset(p, 0, sizeof(*p));
    uint32_t colors = 0;
    bool opaque = analyze(px, w, h, stride, p, &colors);

    uint8_t format = o->format;
    if (format == LIBTODMVI_FORMAT_AUTO)
        format = (colors <= DMVI_PALETTE_MAX) ? DMVI_FORMAT_I8 : opaque ? DMVI_FORMAT_RGB565 : DMVI_FORMAT_RGB565A8;
    layout_t l;
    uint8_t* data = NULL;
    if (format < DMVI_FORMAT_RGB565 || format > DMVI_FORMAT_A4)
        status = -EINVAL;
    else if (format == DMVI_FORMAT_I8 && colors > DMVI_PALETTE_MAX)
        status = -ENOTSUP;
    else if (!lay_out(format, w, h, colors, &l))
        status = -E2BIG;
    else if ((data = Dmod_Malloc(l.end)) == NULL)
        status = -ENOMEM;
    if (status != 0)
    {
        Dmod_Free(p);
        return status;
    }
    memset(data, 0, l.end);
    fill(data, &l, format, px, w, h, stride, opaque, p, !o->no_dither);

    /* The header */
    uint32_t unpacked = l.end - HEADER, packed = 0;
    uint8_t* blob = (compression != NULL) ? pack(compression, o->compression != NULL, data + HEADER, unpacked, &packed, &status) : NULL;
    uint8_t* hd = data;
    memcpy(hd, "DMVI", 4);
    wr16(hd + 4, DMVI_VERSION_MAJOR);
    wr16(hd + 6, DMVI_VERSION_MINOR);
    wr32(hd + 8, HEADER + ((blob != NULL) ? packed : unpacked));
    wr16(hd + 12, w);
    wr16(hd + 14, h);
    hd[16] = format;
    wr16(hd + 18, (format == DMVI_FORMAT_I8) ? p->count : 0U);
    wr32(hd + 20, l.stride);
    wr32(hd + 24, l.pixels);
    wr32(hd + 28, l.alpha_stride);
    wr32(hd + 32, l.alpha);
    wr32(hd + 36, l.palette);
    if (blob != NULL)
        memcpy(hd + 40, compression, strlen(compression));
    wr32(hd + 52, unpacked);

    if (status == 0)
        status = write_file(output, hd, (blob != NULL) ? blob : data + HEADER, (blob != NULL) ? packed : unpacked);
    if (status == 0 && result != NULL)
    {
        result->width = (uint16_t)w;
        result->height = (uint16_t)h;
        result->format = format;
        result->colors = colors;
        result->unpacked_size = unpacked;
        result->size = HEADER + ((blob != NULL) ? packed : unpacked);
        result->compressed = blob != NULL;
    }
    if (blob != NULL)
        Dmod_Free(blob);
    Dmod_Free(data);
    Dmod_Free(p);
    return status;
}
