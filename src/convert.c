#define DMOD_ENABLE_REGISTRATION ON
#include "private.h"
#include "dmimg.h"
#include <errno.h>
#include <string.h>

/*
 * Converting: the image is read with dmimg - at 1/2 ... 1/8 of its size
 * when the decoder can and the image is scaled down that far anyway - kept
 * as 0xAARRGGBB pixels, scaled down to fit the options' size by averaging
 * (with premultiplied alpha, so no dark fringes), and encoded (encode.c).
 */

static const struct
{
    char    name[12];
    uint8_t format;
} g_formats[] = {
    { "auto",     LIBTODMVI_FORMAT_AUTO },
    { "rgb565",   DMVI_FORMAT_RGB565 },
    { "argb8888", DMVI_FORMAT_ARGB8888 },
    { "rgb565a8", DMVI_FORMAT_RGB565A8 },
    { "i8",       DMVI_FORMAT_I8 },
    { "a8",       DMVI_FORMAT_A8 },
    { "a4",       DMVI_FORMAT_A4 },
};

/* The size `w` x `h` scaled down to fit the options' size, the aspect kept */
static void fit(uint32_t w, uint32_t h, const libtodmvi_options_t* o, uint32_t* tw, uint32_t* th)
{
    uint32_t mw = (o->max_width != 0) ? o->max_width : w, mh = (o->max_height != 0) ? o->max_height : h;
    *tw = w;
    *th = h;
    if (w <= mw && h <= mh)
        return;
    if ((uint64_t)w * mh > (uint64_t)h * mw)
    {
        *tw = mw;
        *th = (uint32_t)(((uint64_t)h * mw + w / 2U) / w);
    }
    else
    {
        *th = mh;
        *tw = (uint32_t)(((uint64_t)w * mh + h / 2U) / h);
    }
    if (*tw == 0)
        *tw = 1;
    if (*th == 0)
        *th = 1;
}

/* `src` (sw x sh, `stride` apart) averaged down to tw x th (allocated) */
static uint32_t* shrink(const uint32_t* src, uint32_t sw, uint32_t sh, uint32_t stride, uint32_t tw, uint32_t th)
{
    uint32_t* out = Dmod_Malloc((size_t)tw * th * sizeof(uint32_t));
    if (out == NULL)
        return NULL;
    for (uint32_t ty = 0; ty < th; ty++)
    {
        uint32_t y0 = (uint32_t)((uint64_t)ty * sh / th), y1 = (uint32_t)((uint64_t)(ty + 1U) * sh / th);
        if (y1 <= y0)
            y1 = y0 + 1U;
        for (uint32_t tx = 0; tx < tw; tx++)
        {
            uint32_t x0 = (uint32_t)((uint64_t)tx * sw / tw), x1 = (uint32_t)((uint64_t)(tx + 1U) * sw / tw);
            if (x1 <= x0)
                x1 = x0 + 1U;
            uint64_t r = 0, g = 0, b = 0, a = 0, n = (uint64_t)(x1 - x0) * (y1 - y0);
            for (uint32_t y = y0; y < y1; y++)
            {
                const uint32_t* row = src + (size_t)y * stride;
                for (uint32_t x = x0; x < x1; x++)
                {
                    uint32_t c = row[x], ca = c >> 24;
                    a += ca;
                    r += ((c >> 16) & 0xFFu) * ca;
                    g += ((c >> 8) & 0xFFu) * ca;
                    b += (c & 0xFFu) * ca;
                }
            }
            out[(size_t)ty * tw + tx] = (a == 0) ? 0 :
                (uint32_t)(((a + n / 2U) / n) << 24 | ((r + a / 2U) / a) << 16 | ((g + a / 2U) / a) << 8 | ((b + a / 2U) / a));
        }
    }
    return out;
}

static const libtodmvi_options_t* options_or_defaults(const libtodmvi_options_t* o, libtodmvi_options_t* defaults)
{
    if (o != NULL)
        return o;
    memset(defaults, 0, sizeof(*defaults));
    return defaults;
}

/* Scale `px` to fit and encode it */
static int convert(const uint32_t* px, uint32_t w, uint32_t h, uint32_t stride, const char* output,
                   const libtodmvi_options_t* o, libtodmvi_result_t* result)
{
    uint32_t tw, th;
    fit(w, h, o, &tw, &th);
    if (tw == w && th == h)
        return encode_file(px, w, h, stride, output, o, result);
    uint32_t* small = shrink(px, w, h, stride, tw, th);
    if (small == NULL)
        return -ENOMEM;
    int ret = encode_file(small, tw, th, tw, output, o, result);
    Dmod_Free(small);
    return ret;
}

/* ---- Decoding ---- */

typedef struct
{
    uint32_t*   pixels;
    uint32_t    width, height;
} canvas_t;

/* static: handed out as a callback (a global function's address goes through the GOT) */
static int put_block(void* ctx, const dmimg_block_t* b)
{
    canvas_t* c = ctx;
    if (b->x >= c->width || b->y >= c->height)
        return 0;
    uint32_t w = (b->width < c->width - b->x) ? b->width : c->width - b->x;
    uint32_t h = (b->height < c->height - b->y) ? b->height : c->height - b->y;
    for (uint32_t y = 0; y < h; y++)
        memcpy(c->pixels + (size_t)(b->y + y) * c->width + b->x, b->pixels + (size_t)y * b->stride, w * sizeof(uint32_t));
    return 0;
}

/* ---- API ---- */

dmod_libtodmvi_api_declaration(1.0, int, _convert_pixels, ( const uint32_t* pixels, uint32_t width, uint32_t height, uint32_t stride, const char* output, const libtodmvi_options_t* options, libtodmvi_result_t* result ))
{
    libtodmvi_options_t defaults;
    if (pixels == NULL || output == NULL || width == 0 || height == 0 || stride < width)
        return -EINVAL;
    if (result != NULL)
    {
        memset(result, 0, sizeof(*result));
        result->source_width = width;
        result->source_height = height;
    }
    return convert(pixels, width, height, stride, output, options_or_defaults(options, &defaults), result);
}

dmod_libtodmvi_api_declaration(1.0, int, _convert_file, ( const char* input, const char* output, const libtodmvi_options_t* options, libtodmvi_result_t* result ))
{
    libtodmvi_options_t defaults;
    dmimg_info_t info;
    int status = 0;
    if (input == NULL || output == NULL)
        return -EINVAL;
    const libtodmvi_options_t* o = options_or_defaults(options, &defaults);
    if (result != NULL)
        memset(result, 0, sizeof(*result));

    dmimg_t image = dmimg_open_file(input, &info, &status);
    if (image == NULL)
        return status;
    if (result != NULL)
    {
        const char* decoder = dmimg_decoder_name(image);
        result->source_width = info.width;
        result->source_height = info.height;
        if (decoder != NULL)
        {
            strncpy(result->decoder, decoder, sizeof(result->decoder) - 1U);
            result->decoder[sizeof(result->decoder) - 1U] = '\0';
        }
    }

    /* The smallest scale the decoder has that is not smaller than the output */
    uint32_t tw, th;
    uint8_t scale = 0;
    fit(info.width, info.height, o, &tw, &th);
    for (uint8_t n = 1; n <= DMIMG_MAX_SCALE; n++)
    {
        if ((info.scales & DMIMG_SCALE(n)) != 0 && DMIMG_SCALED(info.width, n) >= tw && DMIMG_SCALED(info.height, n) >= th)
            scale = n;
    }
    canvas_t canvas;
    canvas.width = DMIMG_SCALED(info.width, scale);
    canvas.height = DMIMG_SCALED(info.height, scale);
    if ((uint64_t)canvas.width * canvas.height > 0x3FFFFFFFu / sizeof(uint32_t) ||
        (canvas.pixels = Dmod_Malloc((size_t)canvas.width * canvas.height * sizeof(uint32_t))) == NULL)
    {
        dmimg_close(image);
        return -ENOMEM;
    }
    memset(canvas.pixels, 0, (size_t)canvas.width * canvas.height * sizeof(uint32_t));
    status = dmimg_decode(image, scale, put_block, &canvas);
    dmimg_close(image);
    if (status == 0)
        status = convert(canvas.pixels, canvas.width, canvas.height, canvas.width, output, o, result);
    Dmod_Free(canvas.pixels);
    return status;
}

dmod_libtodmvi_api_declaration(1.0, const char*, _format_name, ( uint8_t format ))
{
    for (size_t i = 0; i < sizeof(g_formats) / sizeof(g_formats[0]); i++)
    {
        if (g_formats[i].format == format)
            return g_formats[i].name;
    }
    return NULL;
}

dmod_libtodmvi_api_declaration(1.0, int, _format_by_name, ( const char* name ))
{
    for (size_t i = 0; name != NULL && i < sizeof(g_formats) / sizeof(g_formats[0]); i++)
    {
        if (strcmp(g_formats[i].name, name) == 0)
            return g_formats[i].format;
    }
    return -1;
}

int dmod_init(const Dmod_Config_t* Config)
{
    (void)Config;
    return 0;
}

int dmod_deinit(void)
{
    return 0;
}
