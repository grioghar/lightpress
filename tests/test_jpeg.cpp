// lightpress — JPEG test suite
#include "lightpress/jpeg.hpp"
#include "lightpress/exif.hpp"
#include "ssim.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace lp;

// Synthetic gradient image
static Image make_gradient(int w, int h) {
    Image img;
    img.width  = w;
    img.height = h;
    img.format = PixelFormat::RGB;
    img.pixels.resize(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
        uint8_t* row = img.row(y);
        for (int x = 0; x < w; ++x) {
            row[x*3+0] = static_cast<uint8_t>(x * 255 / (w-1));
            row[x*3+1] = static_cast<uint8_t>(y * 255 / (h-1));
            row[x*3+2] = static_cast<uint8_t>((x + y) * 128 / (w+h-2));
        }
    }
    return img;
}

static void test_ssim_quality() {
    printf("test_ssim_quality... ");
    Image orig = make_gradient(64, 64);

    // Quality 85
    {
        JpegEncodeOptions opts85; opts85.quality = 85;
        auto enc = jpeg_encode(orig, opts85);
        assert(!enc.empty() && "encode returned empty");
        Image dec = jpeg_decode(enc.data(), enc.size());
        assert(dec.valid() && "decode returned invalid image");
        assert(dec.width == 64 && dec.height == 64);
        double s = ssim(orig, dec);
        printf("SSIM@85=%.4f ", s);
        assert(s >= 0.92 && "SSIM at quality=85 below threshold 0.92");
    }

    // Quality 50
    {
        JpegEncodeOptions opts50; opts50.quality = 50;
        auto enc = jpeg_encode(orig, opts50);
        assert(!enc.empty());
        Image dec = jpeg_decode(enc.data(), enc.size());
        assert(dec.valid());
        double s2 = ssim(orig, dec);
        printf("SSIM@50=%.4f ", s2);
        assert(s2 >= 0.80 && "SSIM at quality=50 below threshold 0.80");
    }

    printf("PASS\n");
}

static void test_compression_ratio() {
    printf("test_compression_ratio... ");
    Image orig = make_gradient(64, 64);
    JpegEncodeOptions opts;
    auto enc = jpeg_encode(orig, opts);
    size_t raw_size = static_cast<size_t>(orig.width) * orig.height * orig.channels();
    assert(!enc.empty());
    assert(enc.size() < raw_size && "JPEG output not smaller than raw pixels");
    printf("raw=%zu jpeg=%zu ratio=%.2fx ", raw_size, enc.size(),
           (double)raw_size / enc.size());
    printf("PASS\n");
}

static void test_exif_strip() {
    printf("test_exif_strip... ");
    // Build a fake JPEG with an APP1 marker
    std::vector<uint8_t> fake_jpeg;
    // SOI
    fake_jpeg.push_back(0xFF); fake_jpeg.push_back(0xD8);
    // APP0 (JFIF)
    fake_jpeg.push_back(0xFF); fake_jpeg.push_back(0xE0);
    fake_jpeg.push_back(0x00); fake_jpeg.push_back(16);
    for (int i=0;i<14;i++) fake_jpeg.push_back(0);
    // APP1 (EXIF) — 20 bytes
    fake_jpeg.push_back(0xFF); fake_jpeg.push_back(0xE1);
    fake_jpeg.push_back(0x00); fake_jpeg.push_back(20);
    for (int i=0;i<18;i++) fake_jpeg.push_back(0xAB);
    // EOI
    fake_jpeg.push_back(0xFF); fake_jpeg.push_back(0xD9);

    auto stripped = strip_jpeg_metadata(fake_jpeg.data(), fake_jpeg.size());
    assert(!stripped.empty());
    // Must not contain 0xFF 0xE1
    for (size_t i = 0; i + 1 < stripped.size(); ++i) {
        assert(!(stripped[i] == 0xFF && stripped[i+1] == 0xE1)
               && "APP1 marker found after strip");
    }
    // Must still start with SOI
    assert(stripped[0] == 0xFF && stripped[1] == 0xD8);
    // Must end with EOI
    assert(stripped[stripped.size()-2] == 0xFF && stripped[stripped.size()-1] == 0xD9);

    printf("PASS\n");
}

static void test_roundtrip_dimensions() {
    printf("test_roundtrip_dimensions... ");
    Image orig = make_gradient(64, 64);
    JpegEncodeOptions opts;
    auto enc1 = jpeg_encode(orig, opts);
    Image dec1 = jpeg_decode(enc1.data(), enc1.size());
    assert(dec1.valid());
    auto enc2 = jpeg_encode(dec1, opts);
    Image dec2 = jpeg_decode(enc2.data(), enc2.size());
    assert(dec2.valid());
    assert(dec2.width == 64 && dec2.height == 64
           && "Dimensions changed after roundtrip");
    printf("PASS\n");
}

static void test_edge_cases() {
    printf("test_edge_cases... ");
    JpegEncodeOptions opts;

    // 1x1
    {
        Image img; img.width=1; img.height=1; img.format=PixelFormat::RGB;
        img.pixels = {128,64,32};
        auto enc = jpeg_encode(img, opts);
        assert(!enc.empty());
        Image dec = jpeg_decode(enc.data(), enc.size());
        assert(dec.width == 1 && dec.height == 1);
    }

    // 1x64
    {
        Image img; img.width=1; img.height=64; img.format=PixelFormat::RGB;
        img.pixels.resize(64*3,128);
        auto enc = jpeg_encode(img, opts);
        assert(!enc.empty());
        Image dec = jpeg_decode(enc.data(), enc.size());
        assert(dec.width == 1 && dec.height == 64);
    }

    // 64x1
    {
        Image img; img.width=64; img.height=1; img.format=PixelFormat::RGB;
        img.pixels.resize(64*3,64);
        auto enc = jpeg_encode(img, opts);
        assert(!enc.empty());
        Image dec = jpeg_decode(enc.data(), enc.size());
        assert(dec.width == 64 && dec.height == 1);
    }

    // 17x13 (not divisible by 8)
    {
        Image img; img.width=17; img.height=13; img.format=PixelFormat::RGB;
        img.pixels.resize(17*13*3,200);
        auto enc = jpeg_encode(img, opts);
        assert(!enc.empty());
        Image dec = jpeg_decode(enc.data(), enc.size());
        assert(dec.width == 17 && dec.height == 13);
    }

    printf("PASS\n");
}

int main() {
    printf("=== JPEG tests ===\n");
    test_ssim_quality();
    test_compression_ratio();
    test_exif_strip();
    test_roundtrip_dimensions();
    test_edge_cases();
    printf("All JPEG tests passed.\n");
    return 0;
}
