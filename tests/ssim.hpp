#pragma once
#include "../include/lightpress/image.hpp"
#include <cmath>
#include <stdexcept>

namespace lp {

// Computes the mean SSIM between two images of identical dimensions and format.
// Returns a value in [-1, 1]; 1.0 = identical, > 0.95 = visually indistinguishable.
// Uses an 8x8 window. Both images must have the same width, height, and format.
//
// SSIM(x,y) = (2*mu_x*mu_y + C1)(2*sigma_xy + C2)
//           / ((mu_x^2 + mu_y^2 + C1)(sigma_x^2 + sigma_y^2 + C2))
// C1 = (0.01 * 255)^2, C2 = (0.03 * 255)^2

inline double ssim(const Image& a, const Image& b) {
    if (a.width != b.width || a.height != b.height || a.format != b.format)
        throw std::invalid_argument("ssim: images must have identical dimensions and format");
    if (!a.valid() || !b.valid())
        throw std::invalid_argument("ssim: invalid images");

    const double C1 = (0.01 * 255) * (0.01 * 255);
    const double C2 = (0.03 * 255) * (0.03 * 255);
    const int W = 8; // window size

    int ch = a.channels();
    int w = a.width;
    int h = a.height;

    double total_ssim = 0.0;
    int num_windows   = 0;

    for (int y = 0; y + W <= h; y += W) {
        for (int x = 0; x + W <= w; x += W) {
            for (int c = 0; c < ch; ++c) {
                double mu_a = 0.0, mu_b = 0.0;
                // Mean
                for (int wy = 0; wy < W; ++wy) {
                    const uint8_t* ra = a.row(y + wy);
                    const uint8_t* rb = b.row(y + wy);
                    for (int wx = 0; wx < W; ++wx) {
                        mu_a += ra[(x + wx)*ch + c];
                        mu_b += rb[(x + wx)*ch + c];
                    }
                }
                int n = W * W;
                mu_a /= n;
                mu_b /= n;

                // Variance and covariance
                double var_a = 0.0, var_b = 0.0, cov = 0.0;
                for (int wy = 0; wy < W; ++wy) {
                    const uint8_t* ra = a.row(y + wy);
                    const uint8_t* rb = b.row(y + wy);
                    for (int wx = 0; wx < W; ++wx) {
                        double da = ra[(x + wx)*ch + c] - mu_a;
                        double db = rb[(x + wx)*ch + c] - mu_b;
                        var_a += da * da;
                        var_b += db * db;
                        cov   += da * db;
                    }
                }
                var_a /= (n - 1);
                var_b /= (n - 1);
                cov   /= (n - 1);

                double num   = (2.0 * mu_a * mu_b + C1) * (2.0 * cov + C2);
                double denom = (mu_a*mu_a + mu_b*mu_b + C1) * (var_a + var_b + C2);
                total_ssim += (denom > 0.0) ? (num / denom) : 1.0;
                ++num_windows;
            }
        }
    }

    return (num_windows > 0) ? (total_ssim / num_windows) : 1.0;
}

} // namespace lp
