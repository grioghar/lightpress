#pragma once
#include "image.hpp"

namespace lp {

enum class ResizeFilter { Nearest, Bilinear, Bicubic };

// Scale `src` to `dst_w` x `dst_h`. Returns a new Image.
Image resize(const Image& src, int dst_w, int dst_h,
             ResizeFilter filter = ResizeFilter::Bilinear);

// Resize to fit within max_dim x max_dim, preserving aspect ratio.
Image resize_to_fit(const Image& src, int max_dim,
                    ResizeFilter filter = ResizeFilter::Bilinear);

} // namespace lp
