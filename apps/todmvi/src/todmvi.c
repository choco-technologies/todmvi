#include "dmod.h"
#include "libtodmvi.h"
#include <errno.h>
#include <string.h>

/**
 * @brief todmvi - convert an image (PNG, JPEG, ... - whatever the dmimg
 *        decoders know) into a dmview image file (.dmvi). See libtodmvi.
 */

#define PATH_MAX_LEN    256

static void print_usage(const char* name)
{
    Dmod_Printf("Usage: %s [options] IMAGE\n", name);
    Dmod_Printf("  -o OUTPUT        the .dmvi file (default: IMAGE with .dmvi in place of its extension)\n");
    Dmod_Printf("  -f FORMAT        auto (default), rgb565, rgb565a8, argb8888, i8, a8, a4\n");
    Dmod_Printf("  -s WIDTHxHEIGHT  scale down to fit (the aspect kept); WIDTHx or xHEIGHT limit one side\n");
    Dmod_Printf("  -c COMPRESSION   fastlz, ... or none (default: fastlz when it makes the file smaller)\n");
    Dmod_Printf("  -b SIGMA         blur (after scaling): a Gaussian blur of that standard deviation in pixels\n");
    Dmod_Printf("  -k X,Y,W,H       keep only that of it (after scaling and blurring): what a box shows\n");
    Dmod_Printf("  -r RADIUS        round its corners (after -k), pixels: outside them transparent\n");
    Dmod_Printf("  --no-dither      round RGB565 colors instead of dithering them\n");
    Dmod_Printf("  -q               print nothing but errors\n");
}

/* IMAGE.png -> IMAGE.dmvi */
static void default_output(const char* input, char* output, size_t size)
{
    const char* dot = strrchr(input, '.');
    const char* slash = strrchr(input, '/');
    size_t len = (dot != NULL && (slash == NULL || dot > slash)) ? (size_t)(dot - input) : strlen(input);
    if (len + sizeof(".dmvi") > size)
        len = size - sizeof(".dmvi");
    memcpy(output, input, len);
    memcpy(output + len, ".dmvi", sizeof(".dmvi"));
}

/* "480x272", "480x", "x272" */
static bool parse_size(const char* s, uint16_t* w, uint16_t* h)
{
    uint32_t v[2] = { 0, 0 };
    int part = 0;
    bool any = false;
    for (; *s != '\0'; s++)
    {
        if (*s == 'x' && part == 0)
            part = 1;
        else if (*s >= '0' && *s <= '9' && v[part] <= 0xFFFFu)
        {
            v[part] = v[part] * 10U + (uint32_t)(*s - '0');
            any = true;
        }
        else
            return false;
    }
    if (part == 0 || !any || v[0] > 0xFFFFu || v[1] > 0xFFFFu)
        return false;
    *w = (uint16_t)v[0];
    *h = (uint16_t)v[1];
    return true;
}

static const char* error_text(int ret)
{
    switch (ret)
    {
        case -ENOENT:  return "cannot read the image";
        case -ENOTSUP: return "not supported (no decoder for it, a format that cannot hold it, or an unknown compression)";
        case -EBADMSG: return "the image is damaged";
        case -E2BIG:   return "too large for a .dmvi (65535 pixels at most)";
        case -ENOMEM:  return "out of memory";
        case -EINVAL:  return "invalid arguments";
        default:       return "cannot write the output";
    }
}

int main(int argc, char* argv[])
{
    const char* input = NULL;
    const char* output = NULL;
    char default_path[PATH_MAX_LEN];
    bool quiet = false;
    libtodmvi_options_t options;
    memset(&options, 0, sizeof(options));

    for (int i = 1; i < argc; i++)
    {
        const char* a = argv[i];
        bool value = i + 1 < argc;
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0)
        {
            print_usage(argv[0]);
            return 0;
        }
        else if (strcmp(a, "-o") == 0 && value)
            output = argv[++i];
        else if (strcmp(a, "-f") == 0 && value)
        {
            int format = libtodmvi_format_by_name(argv[++i]);
            if (format < 0)
            {
                Dmod_Printf("todmvi: unknown format '%s'\n", argv[i]);
                return 1;
            }
            options.format = (uint8_t)format;
        }
        else if (strcmp(a, "-s") == 0 && value)
        {
            if (!parse_size(argv[++i], &options.max_width, &options.max_height))
            {
                Dmod_Printf("todmvi: '%s' is not WIDTHxHEIGHT\n", argv[i]);
                return 1;
            }
        }
        else if (strcmp(a, "-c") == 0 && value)
        {
            i++;
            options.compression = (strcmp(argv[i], "none") == 0) ? "" : argv[i];
        }
        else if (strcmp(a, "-b") == 0 && value)
        {
            uint32_t sigma = 0;
            const char* p = argv[++i];
            for (; *p >= '0' && *p <= '9' && sigma < 1000U; p++)
                sigma = sigma * 10U + (uint32_t)(*p - '0');
            if (*p != '\0' || p == argv[i] || sigma > 255U)
            {
                Dmod_Printf("todmvi: '%s' is not a blur of 0 ... 255 pixels\n", argv[i]);
                return 1;
            }
            options.blur = (uint16_t)sigma;
        }
        else if (strcmp(a, "-k") == 0 && value)
        {
            /* X,Y,W,H */
            uint32_t v[4] = { 0, 0, 0, 0 };
            const char* p = argv[++i];
            int k = 0;
            for (; *p != '\0' && k < 4; p++)
            {
                if (*p == ',')
                    k++;
                else if (*p >= '0' && *p <= '9' && v[k] < 100000U)
                    v[k] = v[k] * 10U + (uint32_t)(*p - '0');
                else
                    break;
            }
            if (*p != '\0' || k != 3 || v[2] == 0 || v[3] == 0 || v[0] > 0xFFFFu || v[1] > 0xFFFFu || v[2] > 0xFFFFu || v[3] > 0xFFFFu)
            {
                Dmod_Printf("todmvi: '%s' is not what to keep (X,Y,W,H)\n", argv[i]);
                return 1;
            }
            options.crop_x = (uint16_t)v[0];
            options.crop_y = (uint16_t)v[1];
            options.crop_w = (uint16_t)v[2];
            options.crop_h = (uint16_t)v[3];
        }
        else if (strcmp(a, "-r") == 0 && value)
        {
            uint32_t r = 0;
            const char* p = argv[++i];
            for (; *p >= '0' && *p <= '9' && r < 100000U; p++)
                r = r * 10U + (uint32_t)(*p - '0');
            if (*p != '\0' || p == argv[i] || r > 0xFFFFu)
            {
                Dmod_Printf("todmvi: '%s' is not a radius in pixels\n", argv[i]);
                return 1;
            }
            options.radius = (uint16_t)r;
        }
        else if (strcmp(a, "--no-dither") == 0)
            options.no_dither = true;
        else if (strcmp(a, "-q") == 0)
            quiet = true;
        else if (input == NULL && a[0] != '-')
            input = a;
        else
        {
            print_usage(argv[0]);
            return 1;
        }
    }
    if (input == NULL)
    {
        print_usage(argv[0]);
        return 1;
    }
    if (output == NULL)
    {
        default_output(input, default_path, sizeof(default_path));
        output = default_path;
    }

    libtodmvi_result_t r;
    int ret = libtodmvi_convert_file(input, output, &options, &r);
    if (ret != 0)
    {
        Dmod_Printf("todmvi: %s: %s\n", input, error_text(ret));
        return 1;
    }
    if (!quiet)
    {
        Dmod_Printf("%s -> %s: %ux%u %s, %u bytes", input, output, (unsigned)r.width, (unsigned)r.height,
                    libtodmvi_format_name(r.format), (unsigned)r.size);
        if (r.compressed)
            Dmod_Printf(" (%u unpacked)", (unsigned)(r.unpacked_size + sizeof(dmvi_header_t)));
        if (r.width != r.source_width || r.height != r.source_height)
            Dmod_Printf(", from %ux%u", (unsigned)r.source_width, (unsigned)r.source_height);
        Dmod_Printf(" [%s]\n", r.decoder);
    }
    return 0;
}
