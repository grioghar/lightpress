// lightpress — Resize test suite
#include "lightpress/resize.hpp"
#include "ssim.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

using namespace lp;

static Image make_gradient(int w, int h) {
    Image img;
    img.width  = w;
    img.height = h;
    img.format = PixelFormat::RGB;
    img.pixels.resize(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
        uint8_t* row = img.row(y);
        for (int x = 0; x < w; ++x) {
            row[x*3+0] = static_cast<uint8_t>(x * 255 / (w > 1 ? w-1 : 1));
            row[x*3+1] = static_cast<uint8_t>(y * 255 / (h > 1 ? h-1 : 1));
            row[x*3+2] = 128;
        }
    }
    return img;
}

static void test_nearest_dimensions() {
    printf("test_nearest_dimensions... ");
    Image src = make_gradient(64, 64);
    Image dst = resize(src, 32, 32, ResizeFilter::Nearest);
    assert(dst.valid() && dst.width == 32 && dst.height == 32);
    assert(dst.format == PixelFormat::RGB);
    printf("PASS\n");
}

static void test_bilinear_dimensions() {
    printf("test_bilinear_dimensions... ");
    Image src = make_gradient(100, 80);
    Image dst = resize(src, 50, 40, ResizeFilter::Bilinear);
    assert(dst.valid() && dst.width == 50 && dst.height == 40);
    printf("PASS\n");
}

static void test_bicubic_dimensions() {
    printf("test_bicubic_dimensions... ");
    Image src = make_gradient(64, 64);
    Image dst = resize(src, 128, 128, ResizeFilter::Bicubic);
    assert(dst.valid() && dst.width == 128 && dst.height == 128);
    printf("PASS\n");
}

static void test_resize_to_fit() {
    printf("test_resize_to_fit... ");
    // Landscape
    {
        Image src = make_gradient(200, 100);
        Image dst = resize_to_fit(src, 64);
        assert(dst.valid());
        assert(dst.width == 64);
        assert(dst.height == 32);
    }
    // Portrait
    {
        Image src = make_gradient(100, 200);
        Image dst = resize_to_fit(src, 64);
        assert(dst.valid());
        assert(dst.height == 64);
        assert(dst.width == 32);
    }
    // Square
    {
        Image src = make_gradient(100, 100);
        Image dst = resize_to_fit(src, 50);
        assert(dst.valid() && dst.width == 50 && dst.height == 50);
    }
    // Already fits
    {
        Image src = make_gradient(30, 20);
        Image dst = resize_to_fit(src, 64);
        assert(dst.valid() && dst.width == 30 && dst.height == 20);
    }
    printf("PASS\n");
}

static void test_identity_resize() {
    printf("test_identity_resize... ");
    Image src = make_gradient(32, 32);
    // Resize to same size — should be identical (or very close)
    Image dst = resize(src, 32, 32, ResizeFilter::Bilinear);
    assert(dst.valid() && dst.width == 32 && dst.height == 32);
    // Pixel values should be identical since we return src unchanged
    assert(src.pixels == dst.pixels && "Identity resize changed pixels");
    printf("PASS\n");
}

static void test_upscale_quality() {
    printf("test_upscale_quality... ");
    // Upscale 32→64 bilinear: SSIM of downscaled result vs original
    Image src32 = make_gradient(32, 32);
    // Downscale a 64-pixel image, then upscale — should recover reasonably
    Image src64 = make_gradient(64, 64);
    Image down  = resize(src64, 32, 32, ResizeFilter::Bilinear);
    Image up    = resize(down, 64, 64, ResizeFilter::Bilinear);
    // SSIM after down+up should still be reasonable
    double s = ssim(src64, up);
    printf("SSIM(down+up bilinear)=%.4f ", s);
    assert(s >= 0.70 && "Bilinear down+up SSIM too low");
    printf("PASS\n");
}

static void test_all_filters_agree_roughly() {
    printf("test_all_filters_agree_roughly... ");
    Image src = make_gradient(64, 64);
    Image r1 = resize(src, 32, 32, ResizeFilter::Nearest);
    Image r2 = resize(src, 32, 32, ResizeFilter::Bilinear);
    Image r3 = resize(src, 32, 32, ResizeFilter::Bicubic);
    assert(r1.valid() && r2.valid() && r3.valid());
    // All should produce similar results (no wild divergence)
    double s12 = ssim(r1, r2);
    double s23 = ssim(r2, r3);
    printf("SSIM(nearest,bilinear)=%.4f SSIM(bilinear,bicubic)=%.4f ", s12, s23);
    assert(s12 >= 0.80 && "Nearest and bilinear diverge too much");
    assert(s23 >= 0.90 && "Bilinear and bicubic diverge too much");
    printf("PASS\n");
}

int main() {
    printf("=== Resize tests ===\n");
    test_nearest_dimensions();
    test_bilinear_dimensions();
    test_bicubic_dimensions();
    test_resize_to_fit();
    test_identity_resize();
    test_upscale_quality();
    test_all_filters_agree_roughly();
    printf("All resize tests passed.\n");
    return 0;
}
