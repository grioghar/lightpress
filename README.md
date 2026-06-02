# lightpress

A zero-dependency C++17 image compression and media container library.

## What it is

lightpress compresses JPEG and PNG images, strips metadata, resizes with quality-preserving filters, and rewrites MP4/MOV containers to remove metadata atoms — all without any external dependencies beyond the C++17 standard library.

## Why it exists

Built for [imessage-exporter-redux](https://github.com/grioghar/imessage-exporter-redux) to compress exported iMessage attachments before archiving. SSIM-based quality metrics are used to validate output against ffmpeg baselines.

## What it does

| Feature | Details |
|---|---|
| **JPEG encode** | AAN fast DCT, BT.601 YCbCr, 4:2:0 chroma, IJG quality scaling, standard Huffman tables, JFIF container |
| **JPEG decode** | Baseline JPEG decoder for roundtrip testing and SSIM calculation |
| **PNG encode** | Optimal per-scanline filter selection, DEFLATE with fixed Huffman, CRC32, IHDR/IDAT/IEND |
| **PNG decode** | Filter reversal + inflate, supports RGB/RGBA/Grayscale, color types 0/2/6 |
| **EXIF strip** | Removes APP1 (EXIF/XMP), APP2 (ICC), APP13 (IPTC) from JPEG at marker level — no re-encode |
| **Resize** | Nearest-neighbour, bilinear, bicubic (Mitchell-Netravali) with sRGB linearization |
| **MP4/MOV strip** | Walks MPEG-4 Part 12 atom tree, removes `udta`/`meta`/cover-art atoms, rewrites size fields |

## What it does not do

- **No video transcoding.** Implementing H.264 or H.265 from scratch is years of engineering. For video transcoding, use ffmpeg. lightpress only rewrites MP4 containers — the encoded bitstreams pass through untouched.
- **No hardware acceleration.** Pure portable C++17.
- **No HDR/wide-gamut.** Assumes 8-bit sRGB.

## Build

Requires CMake 3.16+ and a C++17 compiler (GCC 7+, Clang 5+, MSVC 2019+).

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows with MSVC Build Tools:

```bat
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build -G "NMake Makefiles"
cmake --build build
ctest --test-dir build --output-on-failure
```

## Quick start

```cpp
#include <lightpress/lightpress.hpp>

// Load your image (supply pixel data directly in production)
lp::Image img;
img.width  = 1280;
img.height = 720;
img.format = lp::PixelFormat::RGB;
img.pixels = /* your RGB bytes */;

// Resize to fit within 800px
lp::Image thumb = lp::resize_to_fit(img, 800, lp::ResizeFilter::Bicubic);

// Encode as JPEG at quality 85, stripping any EXIF
lp::JpegEncodeOptions opts;
opts.quality    = 85;
opts.strip_exif = true;
std::vector<uint8_t> jpeg_bytes = lp::jpeg_encode(thumb, opts);

// Strip metadata from an existing JPEG in-place (no re-encode)
std::vector<uint8_t> clean_jpeg = lp::strip_jpeg_metadata(raw_jpeg.data(), raw_jpeg.size());

// Strip metadata from an MP4
std::vector<uint8_t> clean_mp4 = lp::strip_mp4_metadata(raw_mp4.data(), raw_mp4.size());
```

## API reference

### Image

```cpp
lp::Image img;
img.width; img.height; img.format;  // PixelFormat::{RGB,RGBA,Grayscale}
img.pixels;   // std::vector<uint8_t>, row-major, tightly packed
img.valid();  // dimensions consistent with pixels.size()
```

### JPEG

```cpp
lp::JpegEncodeOptions opts;  // quality=85, strip_exif=true, progressive=false
std::vector<uint8_t> lp::jpeg_encode(img, opts);
lp::Image            lp::jpeg_decode(data, len);
std::vector<uint8_t> lp::jpeg_strip_exif(data, len);
```

### PNG

```cpp
lp::PngEncodeOptions opts;   // compression=6, strip_metadata=true
std::vector<uint8_t> lp::png_encode(img, opts);
lp::Image            lp::png_decode(data, len);
```

### EXIF / metadata

```cpp
std::vector<uint8_t> lp::strip_jpeg_metadata(data, len);  // no re-encode
size_t               lp::jpeg_metadata_bytes(data, len);
```

### Resize

```cpp
lp::Image lp::resize(src, dst_w, dst_h, lp::ResizeFilter::Bicubic);
lp::Image lp::resize_to_fit(src, max_dim, lp::ResizeFilter::Bilinear);
// ResizeFilter: Nearest, Bilinear, Bicubic
```

### MP4/MOV

```cpp
lp::ContainerStripOptions opts;  // strip_thumbnails=true, strip_cover_art=true, ...
std::vector<uint8_t> lp::strip_mp4_metadata(data, len, opts);
size_t               lp::mp4_metadata_bytes(data, len);
```

## Benchmarks

*To be filled with real numbers after running `bench_ffmpeg` on representative inputs.*

| Encoder | Image | Size (B) | SSIM | Time (ms) |
|---|---|---|---|---|
| raw pixels | 128×128 gradient | 49152 | 1.000 | — |
| lightpress q=85 | 128×128 gradient | TBD | TBD | TBD |
| ffmpeg -q:v 2 | 128×128 gradient | TBD | TBD | TBD |

Run the benchmark yourself:

```sh
cmake --build build --target bench_ffmpeg
./build/bench_ffmpeg
```

## License

MIT. See [LICENSE](LICENSE).
