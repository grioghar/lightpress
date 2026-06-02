// lightpress — Image resize
// Implements Nearest, Bilinear, Bicubic with sRGB linearization to avoid
// the "dark-edges on resize" artifact caused by interpolating in gamma space.

#include "lightpress/resize.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lp {

// ---------------------------------------------------------------------------
// sRGB linearization / de-linearization
// ---------------------------------------------------------------------------

static inline float srgb_to_linear(float c) {
    // IEC 61966-2-1
    if (c <= 0.04045f)
        return c / 12.92f;
    return std::pow((c + 0.055f) / 1.055f, 2.4f);
}

static inline float linear_to_srgb(float c) {
    if (c <= 0.0f) return 0.0f;
    if (c >= 1.0f) return 1.0f;
    if (c <= 0.0031308f)
        return c * 12.92f;
    return 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

static inline float to_linear(uint8_t v) {
    return srgb_to_linear(v / 255.0f);
}

static inline uint8_t from_linear(float v) {
    float s = linear_to_srgb(v);
    int i = static_cast<int>(s * 255.0f + 0.5f);
    if (i < 0)   i = 0;
    if (i > 255) i = 255;
    return static_cast<uint8_t>(i);
}

// ---------------------------------------------------------------------------
// Nearest-neighbour resize (no gamma correction needed)
// ---------------------------------------------------------------------------

static Image resize_nearest(const Image& src, int dw, int dh) {
    Image dst;
    dst.width  = dw;
    dst.height = dh;
    dst.format = src.format;
    int ch = src.channels();
    dst.pixels.resize(static_cast<size_t>(dw) * dh * ch);

    for (int dy = 0; dy < dh; ++dy) {
        int sy = static_cast<int>((dy + 0.5f) * src.height / dh);
        if (sy >= src.height) sy = src.height - 1;
        const uint8_t* srow = src.row(sy);
        uint8_t* drow = dst.row(dy);

        for (int dx = 0; dx < dw; ++dx) {
            int sx = static_cast<int>((dx + 0.5f) * src.width / dw);
            if (sx >= src.width) sx = src.width - 1;
            for (int c = 0; c < ch; ++c)
                drow[dx*ch + c] = srow[sx*ch + c];
        }
    }
    return dst;
}

// ---------------------------------------------------------------------------
// Bilinear resize with gamma correction
// ---------------------------------------------------------------------------

static Image resize_bilinear(const Image& src, int dw, int dh) {
    Image dst;
    dst.width  = dw;
    dst.height = dh;
    dst.format = src.format;
    int ch = src.channels();
    dst.pixels.resize(static_cast<size_t>(dw) * dh * ch);

    bool do_gamma = (src.format == PixelFormat::RGB || src.format == PixelFormat::RGBA);
    int alpha_ch = (src.format == PixelFormat::RGBA) ? 3 : -1;

    for (int dy = 0; dy < dh; ++dy) {
        float fy = ((dy + 0.5f) * src.height / dh) - 0.5f;
        int y0 = static_cast<int>(std::floor(fy));
        int y1 = y0 + 1;
        float ty = fy - y0;
        y0 = std::max(0, std::min(src.height - 1, y0));
        y1 = std::max(0, std::min(src.height - 1, y1));

        const uint8_t* row0 = src.row(y0);
        const uint8_t* row1 = src.row(y1);
        uint8_t* drow = dst.row(dy);

        for (int dx = 0; dx < dw; ++dx) {
            float fx = ((dx + 0.5f) * src.width / dw) - 0.5f;
            int x0 = static_cast<int>(std::floor(fx));
            int x1 = x0 + 1;
            float tx = fx - x0;
            x0 = std::max(0, std::min(src.width - 1, x0));
            x1 = std::max(0, std::min(src.width - 1, x1));

            for (int c = 0; c < ch; ++c) {
                uint8_t v00 = row0[x0*ch+c];
                uint8_t v10 = row0[x1*ch+c];
                uint8_t v01 = row1[x0*ch+c];
                uint8_t v11 = row1[x1*ch+c];

                float r;
                if (do_gamma && c != alpha_ch) {
                    float f00 = to_linear(v00);
                    float f10 = to_linear(v10);
                    float f01 = to_linear(v01);
                    float f11 = to_linear(v11);
                    float f = f00*(1-tx)*(1-ty) + f10*tx*(1-ty)
                            + f01*(1-tx)*ty     + f11*tx*ty;
                    r = linear_to_srgb(f) * 255.0f;
                } else {
                    r = v00*(1-tx)*(1-ty) + v10*tx*(1-ty)
                      + v01*(1-tx)*ty     + v11*tx*ty;
                }

                int iv = static_cast<int>(r + 0.5f);
                if (iv < 0)   iv = 0;
                if (iv > 255) iv = 255;
                drow[dx*ch+c] = static_cast<uint8_t>(iv);
            }
        }
    }
    return dst;
}

// ---------------------------------------------------------------------------
// Bicubic kernel  (Mitchell-Netravali with B=1/3 C=1/3)
// ---------------------------------------------------------------------------

static inline float cubic_kernel(float x) {
    const float B = 1.0f/3.0f;
    const float C = 1.0f/3.0f;
    float ax = std::abs(x);
    if (ax < 1.0f) {
        return ((12 - 9*B - 6*C) * ax*ax*ax
              + (-18 + 12*B + 6*C) * ax*ax
              + (6 - 2*B)) / 6.0f;
    } else if (ax < 2.0f) {
        return ((-B - 6*C) * ax*ax*ax
              + (6*B + 30*C) * ax*ax
              + (-12*B - 48*C) * ax
              + (8*B + 24*C)) / 6.0f;
    }
    return 0.0f;
}

static Image resize_bicubic(const Image& src, int dw, int dh) {
    Image dst;
    dst.width  = dw;
    dst.height = dh;
    dst.format = src.format;
    int ch = src.channels();
    dst.pixels.resize(static_cast<size_t>(dw) * dh * ch);

    bool do_gamma = (src.format == PixelFormat::RGB || src.format == PixelFormat::RGBA);
    int alpha_ch = (src.format == PixelFormat::RGBA) ? 3 : -1;

    for (int dy = 0; dy < dh; ++dy) {
        float fy = ((dy + 0.5f) * src.height / dh) - 0.5f;
        int cy = static_cast<int>(std::floor(fy));
        float ty = fy - cy;

        uint8_t* drow = dst.row(dy);

        for (int dx = 0; dx < dw; ++dx) {
            float fx = ((dx + 0.5f) * src.width / dw) - 0.5f;
            int cx = static_cast<int>(std::floor(fx));
            float tx = fx - cx;

            for (int c = 0; c < ch; ++c) {
                float acc = 0.0f, wsum = 0.0f;
                for (int m = -1; m <= 2; ++m) {
                    int sy = std::max(0, std::min(src.height-1, cy+m));
                    float wy = cubic_kernel(ty - m);
                    const uint8_t* srow = src.row(sy);

                    for (int n = -1; n <= 2; ++n) {
                        int sx = std::max(0, std::min(src.width-1, cx+n));
                        float wx = cubic_kernel(tx - n);
                        float w = wx * wy;
                        float v;
                        if (do_gamma && c != alpha_ch)
                            v = to_linear(srow[sx*ch+c]);
                        else
                            v = srow[sx*ch+c] / 255.0f;
                        acc  += v * w;
                        wsum += w;
                    }
                }
                float result = (wsum != 0.0f) ? (acc / wsum) : 0.0f;
                uint8_t out_val;
                if (do_gamma && c != alpha_ch) {
                    out_val = from_linear(result);
                } else {
                    int iv = static_cast<int>(result * 255.0f + 0.5f);
                    if (iv < 0)   iv = 0;
                    if (iv > 255) iv = 255;
                    out_val = static_cast<uint8_t>(iv);
                }
                drow[dx*ch+c] = out_val;
            }
        }
    }
    return dst;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Image resize(const Image& src, int dst_w, int dst_h, ResizeFilter filter) {
    if (!src.valid() || dst_w <= 0 || dst_h <= 0) return {};
    if (dst_w == src.width && dst_h == src.height) return src;

    switch (filter) {
        case ResizeFilter::Nearest:  return resize_nearest(src, dst_w, dst_h);
        case ResizeFilter::Bilinear: return resize_bilinear(src, dst_w, dst_h);
        case ResizeFilter::Bicubic:  return resize_bicubic(src, dst_w, dst_h);
    }
    return resize_bilinear(src, dst_w, dst_h);
}

Image resize_to_fit(const Image& src, int max_dim, ResizeFilter filter) {
    if (!src.valid() || max_dim <= 0) return {};
    if (src.width <= max_dim && src.height <= max_dim) return src;

    float scale = static_cast<float>(max_dim) / std::max(src.width, src.height);
    int new_w = std::max(1, static_cast<int>(src.width  * scale));
    int new_h = std::max(1, static_cast<int>(src.height * scale));
    return resize(src, new_w, new_h, filter);
}

} // namespace lp
