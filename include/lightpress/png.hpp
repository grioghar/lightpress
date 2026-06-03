#pragma once
#include "image.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lp {

struct PngEncodeOptions {
    int compression = 6;         // zlib level 0-9; 6 is a good default
    bool strip_metadata = true;  // drop tEXt/iTXt/zTXt/eXIf chunks
};

std::vector<std::uint8_t> png_encode(const Image& img, const PngEncodeOptions& opts = {});
Image png_decode(const std::uint8_t* data, std::size_t len);

} // namespace lp
