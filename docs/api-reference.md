# libtodmvi API Reference

`#include "libtodmvi.h"` - it includes dmview's `dmview_format.h` for the
`.dmvi` structures and formats (`dmvi_format_t`).

## Options

`libtodmvi_options_t` (zeroed: the defaults):

| Field | Meaning |
|-------|---------|
| `format` | `dmvi_format_t`, or `LIBTODMVI_FORMAT_AUTO` (0): I8 for up to 256 colors, else RGB565 when opaque, RGB565A8 when not |
| `max_width`, `max_height` | Scale down to fit, the aspect kept; 0: that side is not limited. Never scaled up |
| `no_dither` | Round RGB565 colors instead of dithering them (ordered 4x4, the pattern of libdmview's gradients) |
| `compression` | NULL: `"fastlz"` when dmod has it and it makes the file smaller; `""`: none; a name: that compression, always |
| `blur` | After scaling, a Gaussian blur of that standard deviation in pixels (three box blurs of the premultiplied pixels, the edges repeated); 0: none |

Formats:

- **RGB565** drops the alpha; **RGB565A8** keeps it in its own plane;
  **ARGB8888** keeps everything;
- **I8** fails with `-ENOTSUP` for more than 256 colors (fully transparent
  pixels count as one color);
- **A8 / A4** are masks for dmview's `ICON`: the alpha - or, for an opaque
  image, the brightness (white covers).

## Result

`libtodmvi_result_t`:

| Field | Meaning |
|-------|---------|
| `source_width`, `source_height` | The image as read |
| `width`, `height`, `format` | The `.dmvi` written |
| `colors` | Different colors of the scaled image, counted up to 257 |
| `unpacked_size` | Bytes after the header, unpacked |
| `size` | Bytes of the file |
| `compressed` | Whether it is packed |
| `decoder` | The dmimg decoder module (`""` for pixels) |

## Functions

### `libtodmvi_convert_file`

```c
int libtodmvi_convert_file(const char* input, const char* output, const libtodmvi_options_t* options, libtodmvi_result_t* result);
```

Read `input` with dmimg (`dmimg_open_file()`: the enabled decoders, or
`dmimg_<extension>` loaded on demand), convert it, write `output`. When
the image is scaled down by half or more and the decoder decodes smaller
(`info.scales`), it is decoded at the smallest such scale, so a large JPEG
for a small screen is cheap. The decoded image is kept as 4 bytes per
pixel while it is converted.

The output is written to `<output>.<pid>-<id>.tmp` and renamed when it is
complete: a reader sees the old file or the new one, a failed conversion
leaves the old one. `options` and `result` may be NULL.

Returns 0, `-ENOENT` (no input), `-ENOTSUP` (no decoder knows it, I8 with
more than 256 colors, a compression dmod does not have), `-EBADMSG` (a
damaged image), `-E2BIG` (larger than 65535 pixels after scaling),
`-ENOMEM`, `-EIO`, `-EINVAL`.

### `libtodmvi_convert_pixels`

```c
int libtodmvi_convert_pixels(const uint32_t* pixels, uint32_t width, uint32_t height, uint32_t stride, const char* output, const libtodmvi_options_t* options, libtodmvi_result_t* result);
```

The same for pixels in memory: 0xAARRGGBB, not premultiplied, `stride`
pixels from one row to the next - e.g. a screenshot.

### `libtodmvi_format_name`, `libtodmvi_format_by_name`

```c
const char* libtodmvi_format_name(uint8_t format);   /* "rgb565", ...; NULL */
int         libtodmvi_format_by_name(const char* name);   /* "auto" -> 0, ...; -1 */
```

Names: `auto`, `rgb565`, `argb8888`, `rgb565a8`, `i8`, `a8`, `a4`.

## todmvi

```
todmvi [options] IMAGE
  -o OUTPUT        the .dmvi file (default: IMAGE with .dmvi in place of its extension)
  -f FORMAT        auto (default), rgb565, rgb565a8, argb8888, i8, a8, a4
  -s WIDTHxHEIGHT  scale down to fit (the aspect kept); WIDTHx or xHEIGHT limit one side
  -c COMPRESSION   fastlz, ... or none (default: fastlz when it makes the file smaller)
  -b SIGMA         blur (after scaling): a Gaussian blur of that standard deviation in pixels
  --no-dither      round RGB565 colors instead of dithering them
  -q               print nothing but errors
```

Exit code 0 on success, 1 on any error (printed).
