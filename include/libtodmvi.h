#ifndef LIBTODMVI_H
#define LIBTODMVI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dmod_types.h"
#include "libtodmvi_defs.h"
#include "dmview_format.h"

/**
 * libtodmvi - converts images into dmview's image files (.dmvi, dmview's
 * docs/image-format.md). The image is read with dmimg - any format a
 * decoder plugin (dmimg_png, dmimg_jpeg, ...) knows - or given as pixels,
 * scaled down to fit a size, turned into one of the .dmvi pixel formats
 * (dithered where colors are reduced) and packed with dmod's compression.
 *
 * The decoded image is kept in memory while it is converted: 4 bytes per
 * pixel at the size it is decoded at - a decoder that decodes smaller (JPEG
 * at 1/2 ... 1/8) is asked to, when the image is scaled down anyway.
 */

/** libtodmvi_options_t.format: chosen from the image. */
#define LIBTODMVI_FORMAT_AUTO       0u

/** Compression level passed to Dmod_Compression_Pack(). */
#define LIBTODMVI_COMPRESSION_LEVEL 2

typedef struct
{
    uint8_t     format;         /**< dmvi_format_t, or LIBTODMVI_FORMAT_AUTO: I8 for up to 256 colors,
                                     else RGB565 when opaque, RGB565A8 when not */
    uint16_t    max_width;      /**< Scaled down to fit, the aspect kept; 0: any width */
    uint16_t    max_height;     /**< 0: any height */
    bool        no_dither;      /**< RGB565 colors rounded, not dithered (ordered 4x4, as libdmview's gradients) */
    const char* compression;    /**< NULL: "fastlz" when dmod has it and it makes the file smaller;
                                     "": none; a name: that one (Dmod_Compression_*) */
} libtodmvi_options_t;

typedef struct
{
    uint32_t    source_width;   /**< The image as it was read */
    uint32_t    source_height;
    uint16_t    width;          /**< The .dmvi */
    uint16_t    height;
    uint8_t     format;         /**< dmvi_format_t written */
    uint32_t    colors;         /**< Different colors (0xAARRGGBB) of the scaled image, counted up to 257 */
    uint32_t    unpacked_size;  /**< Bytes after the header, unpacked */
    uint32_t    size;           /**< Bytes of the file */
    bool        compressed;
    char        decoder[32];    /**< Module that decoded the image ("" for libtodmvi_convert_pixels()) */
} libtodmvi_result_t;

/**
 * @brief Convert the image file @p input into the .dmvi file @p output.
 * The output is written to a temporary file and renamed when complete: a
 * reader sees the old file or the new one, a failed conversion leaves the
 * old one.
 * @param options NULL for the defaults
 * @param result  Receives what was written (may be NULL)
 * @return 0, -ENOENT (no input), -ENOTSUP (no decoder knows it; a format
 *         that cannot hold it - I8 with more than 256 colors; a compression
 *         this dmod does not have), -EBADMSG (a damaged image), -E2BIG
 *         (larger than 65535 pixels after scaling), -ENOMEM, -EIO, -EINVAL
 */
dmod_libtodmvi_api(1.0, int, _convert_file, ( const char* input, const char* output, const libtodmvi_options_t* options, libtodmvi_result_t* result ));

/**
 * @brief Convert pixels - 0xAARRGGBB, @p stride pixels apart - into the
 *        .dmvi file @p output. As libtodmvi_convert_file().
 */
dmod_libtodmvi_api(1.0, int, _convert_pixels, ( const uint32_t* pixels, uint32_t width, uint32_t height, uint32_t stride, const char* output, const libtodmvi_options_t* options, libtodmvi_result_t* result ));

/** @brief Name of a .dmvi pixel format ("rgb565", ...), NULL for none. */
dmod_libtodmvi_api(1.0, const char*, _format_name, ( uint8_t format ));

/** @brief The format named @p name ("rgb565", "auto", ...), -1 for none. */
dmod_libtodmvi_api(1.0, int, _format_by_name, ( const char* name ));

#endif /* LIBTODMVI_H */
