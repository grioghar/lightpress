// lightpress — MP4/MOV container strip test suite
#include "lightpress/container.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace lp;

// ---------------------------------------------------------------------------
// Minimal MP4 builder for tests
// ---------------------------------------------------------------------------

static void push_u32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((x>>24)&0xFF); v.push_back((x>>16)&0xFF);
    v.push_back((x>>8)&0xFF);  v.push_back(x&0xFF);
}

static void push_fourcc(std::vector<uint8_t>& v, const char* s) {
    v.push_back((uint8_t)s[0]); v.push_back((uint8_t)s[1]);
    v.push_back((uint8_t)s[2]); v.push_back((uint8_t)s[3]);
}

// Build an atom: size(4) + type(4) + payload
static std::vector<uint8_t> make_atom(const char* type,
                                       const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> a;
    push_u32(a, (uint32_t)(8 + payload.size()));
    push_fourcc(a, type);
    a.insert(a.end(), payload.begin(), payload.end());
    return a;
}

static bool contains_atom(const std::vector<uint8_t>& data, const char* type) {
    for (size_t i = 0; i + 7 < data.size(); ++i) {
        if (data[i+4] == (uint8_t)type[0] && data[i+5] == (uint8_t)type[1]
         && data[i+6] == (uint8_t)type[2] && data[i+7] == (uint8_t)type[3]) {
            // Verify it's a plausible atom (size >= 8)
            uint32_t sz = ((uint32_t)data[i]<<24)|((uint32_t)data[i+1]<<16)
                         |((uint32_t)data[i+2]<<8)|(uint32_t)data[i+3];
            if (sz >= 8) return true;
        }
    }
    return false;
}

// Build a minimal ftyp + moov + udta file
static std::vector<uint8_t> make_test_mp4(bool include_udta) {
    // ftyp atom
    std::vector<uint8_t> ftyp_payload;
    push_fourcc(ftyp_payload, "mp42");
    push_u32(ftyp_payload, 0);
    push_fourcc(ftyp_payload, "mp42");
    push_fourcc(ftyp_payload, "isom");
    auto ftyp = make_atom("ftyp", ftyp_payload);

    // mdat (fake video data)
    std::vector<uint8_t> mdat_payload(100, 0xAA);
    auto mdat = make_atom("mdat", mdat_payload);

    // moov children
    // mvhd (minimal 108-byte movie header)
    std::vector<uint8_t> mvhd_payload(100, 0);
    auto mvhd = make_atom("mvhd", mvhd_payload);

    std::vector<uint8_t> moov_payload;
    moov_payload.insert(moov_payload.end(), mvhd.begin(), mvhd.end());

    if (include_udta) {
        // udta with some metadata
        std::vector<uint8_t> udta_inner(40, 0xCC);
        auto udta = make_atom("udta", udta_inner);
        moov_payload.insert(moov_payload.end(), udta.begin(), udta.end());
    }

    auto moov = make_atom("moov", moov_payload);

    // Combine
    std::vector<uint8_t> mp4;
    mp4.insert(mp4.end(), ftyp.begin(), ftyp.end());
    mp4.insert(mp4.end(), mdat.begin(), mdat.end());
    mp4.insert(mp4.end(), moov.begin(), moov.end());

    return mp4;
}

static void test_strip_udta() {
    printf("test_strip_udta... ");
    auto mp4 = make_test_mp4(true);
    assert(contains_atom(mp4, "udta") && "Test setup: udta not present");

    ContainerStripOptions opts;
    opts.strip_thumbnails = true;
    auto stripped = strip_mp4_metadata(mp4.data(), mp4.size(), opts);
    assert(!stripped.empty() && "strip_mp4_metadata returned empty");
    assert(!contains_atom(stripped, "udta") && "udta still present after strip");

    // moov and ftyp should still be present
    assert(contains_atom(stripped, "ftyp") && "ftyp missing");
    assert(contains_atom(stripped, "moov") && "moov missing");
    assert(contains_atom(stripped, "mdat") && "mdat missing");

    printf("stripped=%zu → %zu ", mp4.size(), stripped.size());
    printf("PASS\n");
}

static void test_no_strip_without_udta() {
    printf("test_no_strip_without_udta... ");
    auto mp4 = make_test_mp4(false);
    assert(!contains_atom(mp4, "udta"));

    ContainerStripOptions opts;
    auto stripped = strip_mp4_metadata(mp4.data(), mp4.size(), opts);
    assert(!stripped.empty());
    // Should be same size (nothing to remove)
    assert(contains_atom(stripped, "moov"));
    assert(contains_atom(stripped, "ftyp"));
    printf("PASS\n");
}

static void test_metadata_bytes_count() {
    printf("test_metadata_bytes_count... ");
    auto mp4_with = make_test_mp4(true);
    auto mp4_without = make_test_mp4(false);
    size_t bytes_with    = mp4_metadata_bytes(mp4_with.data(),    mp4_with.size());
    size_t bytes_without = mp4_metadata_bytes(mp4_without.data(), mp4_without.size());
    printf("with_udta=%zu without_udta=%zu ", bytes_with, bytes_without);
    assert(bytes_with >= bytes_without && "More metadata in 'with' version expected");
    printf("PASS\n");
}

static void test_empty_input() {
    printf("test_empty_input... ");
    auto result = strip_mp4_metadata(nullptr, 0);
    assert(result.empty() && "Should return empty for null input");
    std::vector<uint8_t> tiny = {0x00, 0x00};
    auto r2 = strip_mp4_metadata(tiny.data(), tiny.size());
    assert(r2.empty() && "Should return empty for tiny input");
    printf("PASS\n");
}

static void test_preserved_atoms() {
    printf("test_preserved_atoms... ");
    auto mp4 = make_test_mp4(true);
    ContainerStripOptions opts;
    auto stripped = strip_mp4_metadata(mp4.data(), mp4.size(), opts);
    assert(!stripped.empty());
    // Essential atoms must survive
    assert(contains_atom(stripped, "mvhd") && "mvhd missing");
    printf("PASS\n");
}

int main() {
    printf("=== Container tests ===\n");
    test_strip_udta();
    test_no_strip_without_udta();
    test_metadata_bytes_count();
    test_empty_input();
    test_preserved_atoms();
    printf("All container tests passed.\n");
    return 0;
}
