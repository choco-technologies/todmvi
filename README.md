# todmvi

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/todmvi/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/todmvi/actions/workflows/ci.yml)

Converts images into dmview's image files (`.dmvi` - see dmview's
`docs/image-format.md`).

## Description

dmview draws one image format: `.dmvi`, raw pixels with nothing to decode.
todmvi makes them from anything a [dmimg](https://github.com/choco-technologies/dmimg)
decoder plugin reads - `dmimg_png`, `dmimg_jpeg`, ... - so the device needs
neither the decoders nor the time to decode:

- **at build time**, on the build host (dmod converts the images of a
  module's fixtures with it, like the views with todmv);
- **on a device** that has todmvi and the decoders (dmod-os, a target with
  more memory) - e.g. a photo viewer converts a JPEG from an SD card.

Two modules, like todmv:

- **libtodmvi** - the conversion, for programs (`libtodmvi_convert_file()`,
  `libtodmvi_convert_pixels()`);
- **todmvi** - the command-line tool.

What a conversion does:

1. reads the image with dmimg - at 1/2 ... 1/8 of its size when the decoder
   can (JPEG) and it is scaled down that far anyway;
2. scales it down to fit a size (`-s`), averaging with premultiplied alpha;
3. writes it in the format given (`-f`) or chosen from the image:
   **I8** (a palette) for up to 256 colors, else **RGB565** when it is
   opaque, **RGB565A8** when it is not. RGB565 colors are dithered with the
   4x4 pattern libdmview uses for gradients. **A8** / **A4** make masks for
   `ICON` - of the alpha, or of the brightness of an opaque image;
4. packs it with dmod's compression (FastLZ) when that makes it smaller.

## Usage

```bash
dmod_loader todmvi.dmf --args logo.png                       # logo.dmvi, format chosen
dmod_loader todmvi.dmf --args photo.jpg -s 480x272 -o bg.dmvi
dmod_loader todmvi.dmf --args wifi.png -f a4                  # a mask for ICON
```

```
todmvi [options] IMAGE
  -o OUTPUT        the .dmvi file (default: IMAGE with .dmvi in place of its extension)
  -f FORMAT        auto (default), rgb565, rgb565a8, argb8888, i8, a8, a4
  -s WIDTHxHEIGHT  scale down to fit (the aspect kept); WIDTHx or xHEIGHT limit one side
  -c COMPRESSION   fastlz, ... or none (default: fastlz when it makes the file smaller)
  --no-dither      round RGB565 colors instead of dithering them
  -q               print nothing but errors
```

```
logo.png -> logo.dmvi: 120x40 i8, 104 bytes (4864 unpacked) [dmimg_png]
photo.png -> bg.dmvi: 363x272 rgb565, 126026 bytes (198072 unpacked), from 640x480 [dmimg_png]
```

The decoder of a file is found by its first bytes among the enabled
decoders, or loaded by the file's extension (`dmimg_png` for `.png`) - it
has to be installed where dmod finds modules (`DMOD_DMF_DIR`).

## Documentation

- [docs/api-reference.md](docs/api-reference.md) - libtodmvi

## Building

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

Pass `-DDMOD_DIR=/path/to/local/dmod` to build against a local dmod checkout
instead of fetching `develop` from GitHub.

## Testing

The tests convert pixels and image files of a decoder of their own
(`dmimg_traw`, tests/traw/) and read the `.dmvi` files back:

```bash
cd build
ctest --output-on-failure
```

## License

MIT - see [LICENSE](LICENSE).
