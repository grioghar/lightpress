// lightpress — PNG encoder/decoder
// Implements: PNG signature, IHDR, optimal filter selection per scanline,
// DEFLATE with fixed Huffman (RFC 1951 §3.2.6), CRC32, IDAT, IEND.
// PNG decode uses a simple filter-reversal + inflate path for roundtrip testing.

#include "lightpress/png.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace lp {

// ---------------------------------------------------------------------------
// CRC32 (ISO 3309 polynomial used by PNG)
// ---------------------------------------------------------------------------
static uint32_t crc32_table[256];
static bool crc32_initialized = false;

static void init_crc32() {
    if (crc32_initialized) return;
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_table[n] = c;
    }
    crc32_initialized = true;
}

static uint32_t crc32(const uint8_t* data, size_t len, uint32_t prev = 0xFFFFFFFFu) {
    init_crc32();
    uint32_t c = prev;
    for (size_t i = 0; i < len; ++i)
        c = crc32_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// Adler-32 (used by zlib)
// ---------------------------------------------------------------------------
static uint32_t adler32(const uint8_t* data, size_t len, uint32_t prev = 1) {
    uint32_t s1 = prev & 0xFFFF;
    uint32_t s2 = (prev >> 16) & 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        s1 = (s1 + data[i]) % 65521;
        s2 = (s2 + s1)      % 65521;
    }
    return (s2 << 16) | s1;
}

// ---------------------------------------------------------------------------
// PNG filter functions
// ---------------------------------------------------------------------------
static inline uint8_t paeth(int a, int b, int c) {
    int p  = a + b - c;
    int pa = std::abs(p - a);
    int pb = std::abs(p - b);
    int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<uint8_t>(a);
    if (pb <= pc)             return static_cast<uint8_t>(b);
    return static_cast<uint8_t>(c);
}

// Compute filtered row and return sum of abs values (filter cost estimate)
static int apply_filter(int type, const uint8_t* row, const uint8_t* prev,
                        int stride, int ch, uint8_t* out) {
    int cost = 0;
    for (int x = 0; x < stride; ++x) {
        int a = (x >= ch) ? row[x - ch] : 0;
        int b = prev ? (int)prev[x] : 0;
        int c = (prev && x >= ch) ? (int)prev[x - ch] : 0;
        int s = (int)row[x];
        uint8_t f;
        switch (type) {
            case 0: f = (uint8_t)s;                               break;
            case 1: f = (uint8_t)(s - a);                        break;
            case 2: f = (uint8_t)(s - b);                        break;
            case 3: f = (uint8_t)(s - (a + b) / 2);             break;
            case 4: f = (uint8_t)(s - paeth(a, b, c));          break;
            default: f = (uint8_t)s; break;
        }
        out[x] = f;
        cost += (f <= 127) ? f : (256 - f);
    }
    return cost;
}

// ---------------------------------------------------------------------------
// DEFLATE encoder — fixed Huffman, uncompressed stored blocks fallback
// We implement stored (non-compressed) blocks for simplicity + correctness.
// For a real deployment the user can link system zlib; here we implement a
// simple LZ77 + fixed Huffman encoder.
// ---------------------------------------------------------------------------

// Fixed Huffman codes (RFC 1951 §3.2.6)
static uint16_t fixed_ll_code[288];   // literal/length codes
static uint8_t  fixed_ll_bits[288];
static uint16_t fixed_dist_code[32];
static uint8_t  fixed_dist_bits[32];
static bool     fixed_huffman_built = false;

static void build_fixed_huffman() {
    if (fixed_huffman_built) return;
    // Literal/length codes
    for (int i = 0;   i <= 143; ++i) { fixed_ll_bits[i] = 8; fixed_ll_code[i] = (uint16_t)(0x0030 + i); }
    for (int i = 144; i <= 255; ++i) { fixed_ll_bits[i] = 9; fixed_ll_code[i] = (uint16_t)(0x0190 + (i-144)); }
    for (int i = 256; i <= 279; ++i) { fixed_ll_bits[i] = 7; fixed_ll_code[i] = (uint16_t)(0x0000 + (i-256)); }
    for (int i = 280; i <= 287; ++i) { fixed_ll_bits[i] = 8; fixed_ll_code[i] = (uint16_t)(0x00C0 + (i-280)); }

    // Reverse bits (deflate sends LSB first for Huffman codes)
    auto rev_bits = [](uint16_t code, int nbits) -> uint16_t {
        uint16_t r = 0;
        for (int i = 0; i < nbits; ++i) {
            r = (r << 1) | (code & 1);
            code >>= 1;
        }
        return r;
    };
    for (int i = 0; i < 288; ++i)
        fixed_ll_code[i] = rev_bits(fixed_ll_code[i], fixed_ll_bits[i]);

    // Distance codes: all 5 bits
    for (int i = 0; i < 32; ++i) {
        fixed_dist_bits[i] = 5;
        fixed_dist_code[i] = rev_bits((uint16_t)i, 5);
    }
    fixed_huffman_built = true;
}

struct DeflateWriter {
    std::vector<uint8_t>& out;
    uint32_t buf  = 0;
    int      bits = 0;

    explicit DeflateWriter(std::vector<uint8_t>& o) : out(o) {}

    void write_bits(uint32_t code, int n) {
        buf |= (code << bits);
        bits += n;
        while (bits >= 8) {
            out.push_back(static_cast<uint8_t>(buf & 0xFF));
            buf >>= 8;
            bits -= 8;
        }
    }

    void flush_byte() {
        if (bits > 0) {
            out.push_back(static_cast<uint8_t>(buf & 0xFF));
            buf = 0; bits = 0;
        }
    }

    void write_literal(uint8_t lit) {
        write_bits(fixed_ll_code[lit], fixed_ll_bits[lit]);
    }

    void write_eob() {
        write_bits(fixed_ll_code[256], fixed_ll_bits[256]);
    }
};

// Length/distance tables for LZ77 back-references
// We'll skip LZ77 and just emit literals for simplicity — still correct DEFLATE.
static std::vector<uint8_t> deflate_compress(const uint8_t* data, size_t len) {
    build_fixed_huffman();

    std::vector<uint8_t> out;
    out.reserve(len + len/10 + 32);

    // zlib header: CMF=0x78 (deflate, window=32K), FLG=0x9C (level 6, no dict)
    out.push_back(0x78);
    out.push_back(0x9C);

    // Simple LZ77 with hash-chain for moderate compression
    // For now: literal-only DEFLATE (100% correct, ~0% compression beyond filter)
    // We split into 64KB blocks
    const size_t BLOCK_SIZE = 65535;
    size_t offset = 0;

    while (offset < len) {
        size_t block_len = std::min(BLOCK_SIZE, len - offset);
        bool is_last = (offset + block_len >= len);

        DeflateWriter dw(out);
        // BFINAL | BTYPE=01 (fixed Huffman)
        dw.write_bits(is_last ? 1 : 0, 1); // BFINAL
        dw.write_bits(1, 1); // BTYPE LSB = 1
        dw.write_bits(0, 1); // BTYPE MSB = 0 (fixed Huffman = 01)

        for (size_t i = 0; i < block_len; ++i)
            dw.write_literal(data[offset + i]);
        dw.write_eob();
        dw.flush_byte();

        offset += block_len;
    }

    // Adler-32 checksum (big-endian)
    uint32_t a32 = adler32(data, len);
    out.push_back((a32 >> 24) & 0xFF);
    out.push_back((a32 >> 16) & 0xFF);
    out.push_back((a32 >>  8) & 0xFF);
    out.push_back((a32 >>  0) & 0xFF);

    return out;
}

// ---------------------------------------------------------------------------
// PNG chunk writing
// ---------------------------------------------------------------------------
static void write_u32_be(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((x >> 24) & 0xFF);
    v.push_back((x >> 16) & 0xFF);
    v.push_back((x >>  8) & 0xFF);
    v.push_back((x >>  0) & 0xFF);
}

static void write_chunk(std::vector<uint8_t>& out,
                         const char type[4],
                         const uint8_t* data, size_t len) {
    write_u32_be(out, static_cast<uint32_t>(len));
    size_t crc_start = out.size();
    out.push_back((uint8_t)type[0]);
    out.push_back((uint8_t)type[1]);
    out.push_back((uint8_t)type[2]);
    out.push_back((uint8_t)type[3]);
    out.insert(out.end(), data, data + len);
    uint32_t c = crc32(out.data() + crc_start, 4 + len);
    write_u32_be(out, c);
}

// ---------------------------------------------------------------------------
// PNG encode
// ---------------------------------------------------------------------------
std::vector<uint8_t> png_encode(const Image& img, const PngEncodeOptions& opts) {
    (void)opts; // compression level: we use fixed Huffman regardless
    if (!img.valid()) return {};

    int w = img.width, h = img.height, ch = img.channels();

    // Determine PNG color type
    uint8_t color_type;
    switch (img.format) {
        case PixelFormat::RGB:       color_type = 2; break;
        case PixelFormat::RGBA:      color_type = 6; break;
        case PixelFormat::Grayscale: color_type = 0; break;
        default: color_type = 2;
    }

    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(w) * h * ch + 1024);

    // PNG signature
    const uint8_t sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    out.insert(out.end(), sig, sig + 8);

    // IHDR
    {
        uint8_t ihdr[13];
        ihdr[0] = (w >> 24) & 0xFF; ihdr[1] = (w >> 16) & 0xFF;
        ihdr[2] = (w >>  8) & 0xFF; ihdr[3] = (w >>  0) & 0xFF;
        ihdr[4] = (h >> 24) & 0xFF; ihdr[5] = (h >> 16) & 0xFF;
        ihdr[6] = (h >>  8) & 0xFF; ihdr[7] = (h >>  0) & 0xFF;
        ihdr[8]  = 8;           // bit depth
        ihdr[9]  = color_type;
        ihdr[10] = 0;           // compression method
        ihdr[11] = 0;           // filter method
        ihdr[12] = 0;           // interlace method
        write_chunk(out, "IHDR", ihdr, 13);
    }

    // Build filtered image data
    int stride = w * ch;
    std::vector<uint8_t> filtered;
    filtered.reserve(static_cast<size_t>(h) * (1 + stride));

    std::vector<uint8_t> tmp0(stride), tmp1(stride), tmp2(stride),
                          tmp3(stride), tmp4(stride);

    for (int y = 0; y < h; ++y) {
        const uint8_t* row  = img.row(y);
        const uint8_t* prev = (y > 0) ? img.row(y - 1) : nullptr;

        // Try all 5 filter types, pick cheapest
        int cost[5];
        uint8_t* bufs[5] = { tmp0.data(), tmp1.data(), tmp2.data(), tmp3.data(), tmp4.data() };
        for (int f = 0; f < 5; ++f)
            cost[f] = apply_filter(f, row, prev, stride, ch, bufs[f]);

        int best = 0;
        for (int f = 1; f < 5; ++f)
            if (cost[f] < cost[best]) best = f;

        filtered.push_back(static_cast<uint8_t>(best));
        filtered.insert(filtered.end(), bufs[best], bufs[best] + stride);
    }

    // DEFLATE compress
    std::vector<uint8_t> compressed = deflate_compress(filtered.data(), filtered.size());

    // IDAT
    write_chunk(out, "IDAT", compressed.data(), compressed.size());

    // IEND
    write_chunk(out, "IEND", nullptr, 0);

    return out;
}

// ---------------------------------------------------------------------------
// PNG decode — for roundtrip testing
// Supports baseline PNG: bit depth 8, color types 0/2/6, no interlacing.
// ---------------------------------------------------------------------------

static uint32_t read_u32_be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
          | ((uint32_t)p[2] <<  8) | (uint32_t)p[3];
}

// Inflate stored-block or fixed-Huffman DEFLATE.
// We use a straightforward bit reader.
struct InflateReader {
    const uint8_t* src;
    size_t src_len;
    size_t pos = 0;
    uint32_t buf = 0;
    int bits_avail = 0;

    int read_bit() {
        if (bits_avail == 0) {
            if (pos >= src_len) return -1;
            buf = src[pos++];
            bits_avail = 8;
        }
        int b = buf & 1;
        buf >>= 1;
        --bits_avail;
        return b;
    }

    uint32_t read_bits(int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) {
            int b = read_bit();
            if (b < 0) break;
            v |= ((uint32_t)b << i);
        }
        return v;
    }

    void skip_to_byte() {
        while (bits_avail > 0) { read_bit(); }
    }

    uint16_t read_u16_le() {
        skip_to_byte();
        if (pos + 1 >= src_len) return 0;
        uint16_t v = (uint16_t)(src[pos] | (src[pos+1] << 8));
        pos += 2;
        return v;
    }
};

// Build canonical Huffman decode table from code lengths
struct InflateHTree {
    struct Entry { uint16_t symbol; uint8_t bits; };
    // Fast lookup table: 512 entries (9-bit index)
    static const int FAST_BITS = 9;
    Entry fast[1 << FAST_BITS];
    bool valid = false;

    void build(const uint8_t* lengths, int n) {
        // Count codes per length
        int bl_count[16] = {};
        for (int i = 0; i < n; ++i) if (lengths[i]) bl_count[lengths[i]]++;

        // Starting codes
        int next_code[16] = {};
        int code = 0;
        bl_count[0] = 0;
        for (int bits = 1; bits <= 15; ++bits) {
            code = (code + bl_count[bits-1]) << 1;
            next_code[bits] = code;
        }

        // Fill fast table
        std::memset(fast, 0xFF, sizeof(fast)); // invalid by default
        for (int i = 0; i < n; ++i) {
            int l = lengths[i];
            if (l == 0) continue;
            int c = next_code[l]++;
            if (l <= FAST_BITS) {
                // Fill all entries that match this prefix
                int stride = 1 << l;
                // The code occupies the low l bits; the fast table uses reversed bits
                int rev = 0;
                for (int b = 0; b < l; ++b) rev = (rev << 1) | ((c >> b) & 1);
                for (int j = rev; j < (1 << FAST_BITS); j += stride) {
                    fast[j].symbol = (uint16_t)i;
                    fast[j].bits   = (uint8_t)l;
                }
            }
        }
        valid = true;
    }

    // Decode one symbol, returning -1 on error
    int decode(InflateReader& r) {
        // Peek up to FAST_BITS
        uint32_t peek = 0;
        int saved_bits = r.bits_avail;
        uint32_t saved_buf = r.buf;
        size_t saved_pos = r.pos;
        for (int i = 0; i < FAST_BITS; ++i) {
            int b = r.read_bit();
            if (b < 0) break;
            peek |= ((uint32_t)b << i);
        }
        // Restore reader
        r.bits_avail = saved_bits;
        r.buf = saved_buf;
        r.pos = saved_pos;

        auto& e = fast[peek & ((1 << FAST_BITS) - 1)];
        if (e.bits != 0xFF) {
            // consume e.bits
            r.read_bits(e.bits);
            return e.symbol;
        }
        return -1; // shouldn't happen for our self-encoded data
    }
};

// Build fixed Huffman trees for inflate
static void build_fixed_inflate(InflateHTree& ll, InflateHTree& dist) {
    uint8_t ll_lengths[288];
    for (int i = 0;   i <= 143; ++i) ll_lengths[i] = 8;
    for (int i = 144; i <= 255; ++i) ll_lengths[i] = 9;
    for (int i = 256; i <= 279; ++i) ll_lengths[i] = 7;
    for (int i = 280; i <= 287; ++i) ll_lengths[i] = 8;
    ll.build(ll_lengths, 288);

    uint8_t dist_lengths[32];
    for (int i = 0; i < 32; ++i) dist_lengths[i] = 5;
    dist.build(dist_lengths, 32);
}

// Length/extra bits tables
static const int LENGTH_BASE[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
    35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const int LENGTH_EXTRA[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,
    3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const int DIST_BASE[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
    257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const int DIST_EXTRA[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
    7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

static std::vector<uint8_t> inflate(const uint8_t* data, size_t len) {
    if (len < 2) return {};
    // Skip zlib header (2 bytes)
    InflateReader r;
    r.src = data + 2;
    r.src_len = len - 2 - 4; // exclude 4-byte adler32 at end
    r.pos = 0;

    std::vector<uint8_t> out;
    out.reserve(len * 4);

    bool done = false;
    while (!done && r.pos < r.src_len) {
        int bfinal = (int)r.read_bits(1);
        int btype  = (int)r.read_bits(2);

        if (btype == 0) {
            // Stored block
            r.skip_to_byte();
            uint16_t nlen  = r.read_u16_le();
            uint16_t nlenc = r.read_u16_le();
            (void)nlenc;
            for (uint16_t i = 0; i < nlen && r.pos < r.src_len; ++i)
                out.push_back(r.src[r.pos++]);
        } else if (btype == 1) {
            // Fixed Huffman
            InflateHTree ll, dist;
            build_fixed_inflate(ll, dist);

            while (true) {
                int sym = ll.decode(r);
                if (sym < 0 || sym == 256) break;
                if (sym < 256) {
                    out.push_back((uint8_t)sym);
                } else {
                    // Length
                    int lidx = sym - 257;
                    if (lidx < 0 || lidx >= 29) break;
                    int length = LENGTH_BASE[lidx] + (int)r.read_bits(LENGTH_EXTRA[lidx]);

                    int dsym = dist.decode(r);
                    if (dsym < 0 || dsym >= 30) break;
                    int distance = DIST_BASE[dsym] + (int)r.read_bits(DIST_EXTRA[dsym]);

                    size_t start = out.size();
                    for (int i = 0; i < length; ++i) {
                        size_t from = start - distance + i;
                        out.push_back((from < start) ? out[from] : 0);
                    }
                }
            }
        } else if (btype == 2) {
            // Dynamic Huffman — skip (our encoder never produces this)
            break;
        }

        if (bfinal) done = true;
    }

    return out;
}

// Reverse PNG filter for one row
static void unfilter_row(int type, uint8_t* row, const uint8_t* prev,
                          int stride, int ch) {
    for (int x = 0; x < stride; ++x) {
        int a = (x >= ch) ? (int)row[x - ch] : 0;
        int b = prev ? (int)prev[x] : 0;
        int c = (prev && x >= ch) ? (int)prev[x - ch] : 0;
        int s = (int)row[x];
        switch (type) {
            case 0: break;
            case 1: row[x] = (uint8_t)(s + a); break;
            case 2: row[x] = (uint8_t)(s + b); break;
            case 3: row[x] = (uint8_t)(s + (a + b) / 2); break;
            case 4: row[x] = (uint8_t)(s + paeth(a, b, c)); break;
        }
    }
}

Image png_decode(const uint8_t* data, size_t len) {
    if (len < 8) return {};
    // Check PNG signature
    const uint8_t sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (std::memcmp(data, sig, 8) != 0) return {};

    size_t pos = 8;
    int width = 0, height = 0, color_type = 0;
    std::vector<uint8_t> idat_data;

    while (pos + 12 <= len) {
        uint32_t chunk_len  = read_u32_be(data + pos);
        char type[5] = {};
        type[0] = (char)data[pos+4]; type[1] = (char)data[pos+5];
        type[2] = (char)data[pos+6]; type[3] = (char)data[pos+7];
        const uint8_t* chunk_data = data + pos + 8;
        pos += 8 + chunk_len + 4; // skip length+type+data+crc

        if (std::strcmp(type, "IHDR") == 0 && chunk_len >= 13) {
            width      = (int)read_u32_be(chunk_data);
            height     = (int)read_u32_be(chunk_data + 4);
            // bit_depth = chunk_data[8]; // assume 8
            color_type = chunk_data[9];
        } else if (std::strcmp(type, "IDAT") == 0) {
            idat_data.insert(idat_data.end(), chunk_data, chunk_data + chunk_len);
        } else if (std::strcmp(type, "IEND") == 0) {
            break;
        }
    }

    if (width == 0 || height == 0 || idat_data.empty()) return {};

    // Inflate
    std::vector<uint8_t> raw = inflate(idat_data.data(), idat_data.size());
    if (raw.empty()) return {};

    int ch;
    PixelFormat fmt;
    switch (color_type) {
        case 0: ch = 1; fmt = PixelFormat::Grayscale; break;
        case 2: ch = 3; fmt = PixelFormat::RGB;       break;
        case 6: ch = 4; fmt = PixelFormat::RGBA;      break;
        default: return {};
    }

    int stride = width * ch;
    if (raw.size() < (size_t)height * (1 + stride)) return {};

    Image img;
    img.width  = width;
    img.height = height;
    img.format = fmt;
    img.pixels.resize(static_cast<size_t>(width) * height * ch);

    for (int y = 0; y < height; ++y) {
        int filter = raw[y * (1 + stride)];
        uint8_t* row = img.row(y);
        const uint8_t* prev = (y > 0) ? img.row(y - 1) : nullptr;
        std::memcpy(row, &raw[y * (1 + stride) + 1], stride);
        unfilter_row(filter, row, prev, stride, ch);
    }

    return img;
}

} // namespace lp
