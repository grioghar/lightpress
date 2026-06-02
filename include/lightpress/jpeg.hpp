#pragma once
#include "image.hpp"
#include <cstdint>
#include <vector>

namespace lp {

struct JpegEncodeOptions {
    int quality = 85;          // 1-100; 85 is visually lossless for typical photos
    bool strip_exif = true;    // don't copy EXIF from source into output
    bool progressive = false;
};

// Encode `img` as JPEG. Returns the compressed bytes.
std::vector<uint8_t> jpeg_encode(const Image& img, const JpegEncodeOptions& opts = {});

// Decode a JPEG byte buffer into an Image (used for roundtrip tests and SSIM calc).
Image jpeg_decode(const uint8_t* data, size_t len);

// Strip EXIF/APP1/APP2 markers from an existing JPEG byte stream.
// Returns the re-encoded JFIF without metadata. Does not re-encode pixel data.
std::vector<uint8_t> jpeg_strip_exif(const uint8_t* data, size_t len);

} // namespace lp
