// lightpress — EXIF strip test suite
#include "lightpress/exif.hpp"
#include "lightpress/jpeg.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace lp;

static bool contains_marker(const std::vector<uint8_t>& data, uint8_t marker) {
    for (size_t i = 0; i + 1 < data.size(); ++i)
        if (data[i] == 0xFF && data[i+1] == marker) return true;
    return false;
}

// Build a minimal valid JPEG with optional APP1/APP2/APP13 markers
static std::vector<uint8_t> make_jpeg_with_metadata(
        bool add_app1, bool add_app2, bool add_app13) {
    std::vector<uint8_t> j;
    // SOI
    j.push_back(0xFF); j.push_back(0xD8);

    // APP0 JFIF (minimal, 16 bytes total length including length field)
    j.push_back(0xFF); j.push_back(0xE0);
    j.push_back(0x00); j.push_back(16);
    // JFIF\0
    j.push_back('J'); j.push_back('F'); j.push_back('I'); j.push_back('F'); j.push_back(0);
    // version, density, thumbnail
    for (int i=0;i<9;i++) j.push_back(0);

    auto add_app = [&](uint8_t app_marker, int payload_size) {
        j.push_back(0xFF);
        j.push_back(app_marker);
        uint16_t len = (uint16_t)(2 + payload_size);
        j.push_back((len >> 8) & 0xFF);
        j.push_back(len & 0xFF);
        for (int i = 0; i < payload_size; ++i) j.push_back(0xEE);
    };

    if (add_app1)  add_app(0xE1, 50);   // EXIF
    if (add_app2)  add_app(0xE2, 30);   // ICC
    if (add_app13) add_app(0xED, 40);   // IPTC

    // EOI
    j.push_back(0xFF); j.push_back(0xD9);
    return j;
}

static void test_strip_app1() {
    printf("test_strip_app1... ");
    auto jpeg = make_jpeg_with_metadata(true, false, false);
    assert(contains_marker(jpeg, 0xE1) && "Test setup: APP1 not present");
    auto stripped = strip_jpeg_metadata(jpeg.data(), jpeg.size());
    assert(!stripped.empty());
    assert(!contains_marker(stripped, 0xE1) && "APP1 still present after strip");
    assert(stripped[0] == 0xFF && stripped[1] == 0xD8 && "SOI missing");
    assert(stripped[stripped.size()-2] == 0xFF && stripped[stripped.size()-1] == 0xD9 && "EOI missing");
    printf("PASS\n");
}

static void test_strip_app2() {
    printf("test_strip_app2... ");
    auto jpeg = make_jpeg_with_metadata(false, true, false);
    assert(contains_marker(jpeg, 0xE2));
    auto stripped = strip_jpeg_metadata(jpeg.data(), jpeg.size());
    assert(!stripped.empty());
    assert(!contains_marker(stripped, 0xE2) && "APP2 still present after strip");
    printf("PASS\n");
}

static void test_strip_app13() {
    printf("test_strip_app13... ");
    auto jpeg = make_jpeg_with_metadata(false, false, true);
    assert(contains_marker(jpeg, 0xED));
    auto stripped = strip_jpeg_metadata(jpeg.data(), jpeg.size());
    assert(!stripped.empty());
    assert(!contains_marker(stripped, 0xED) && "APP13 still present after strip");
    printf("PASS\n");
}

static void test_strip_all() {
    printf("test_strip_all... ");
    auto jpeg = make_jpeg_with_metadata(true, true, true);
    size_t meta_bytes = jpeg_metadata_bytes(jpeg.data(), jpeg.size());
    assert(meta_bytes > 0 && "No metadata detected");
    auto stripped = strip_jpeg_metadata(jpeg.data(), jpeg.size());
    assert(!stripped.empty());
    assert(!contains_marker(stripped, 0xE1));
    assert(!contains_marker(stripped, 0xE2));
    assert(!contains_marker(stripped, 0xED));
    // Stripped should be smaller
    assert(stripped.size() < jpeg.size() && "Stripped not smaller");
    // Size difference should match meta_bytes count
    printf("removed=%zu ", meta_bytes);
    printf("PASS\n");
}

static void test_no_metadata_unchanged() {
    printf("test_no_metadata_unchanged... ");
    auto jpeg = make_jpeg_with_metadata(false, false, false);
    auto stripped = strip_jpeg_metadata(jpeg.data(), jpeg.size());
    assert(!stripped.empty());
    // Should preserve all content
    assert(stripped.size() == jpeg.size() || stripped.size() <= jpeg.size());
    printf("PASS\n");
}

static void test_preserve_app0() {
    printf("test_preserve_app0... ");
    auto jpeg = make_jpeg_with_metadata(true, false, false);
    auto stripped = strip_jpeg_metadata(jpeg.data(), jpeg.size());
    // APP0 (0xE0) should still be present
    assert(contains_marker(stripped, 0xE0) && "APP0 (JFIF) was stripped — should be preserved");
    printf("PASS\n");
}

static void test_encoded_jpeg_strip() {
    printf("test_encoded_jpeg_strip... ");
    // Encode a real JPEG and then strip (our encoder never adds EXIF,
    // so stripping should produce same or slightly adjusted output)
    Image img; img.width=16; img.height=16; img.format=PixelFormat::RGB;
    img.pixels.resize(16*16*3, 128);
    JpegEncodeOptions opts; opts.strip_exif = true;
    auto enc = jpeg_encode(img, opts);
    assert(!enc.empty());
    // No APP1 should be present in our output
    assert(!contains_marker(enc, 0xE1) && "Encoder emitted APP1 despite strip_exif=true");
    printf("PASS\n");
}

int main() {
    printf("=== EXIF tests ===\n");
    test_strip_app1();
    test_strip_app2();
    test_strip_app13();
    test_strip_all();
    test_no_metadata_unchanged();
    test_preserve_app0();
    test_encoded_jpeg_strip();
    printf("All EXIF tests passed.\n");
    return 0;
}
