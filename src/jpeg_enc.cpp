// lightpress — JPEG encoder
// Implements: BT.601 RGB→YCbCr, 4:2:0 chroma subsampling,
// AAN fast 8x8 DCT, IJG quality scaling, standard Huffman tables,
// JFIF container output.

#include "lightpress/jpeg.hpp"
#include "lightpress/exif.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace lp {

// ---------------------------------------------------------------------------
// Standard JPEG quantization tables (JPEG Annex K)
// ---------------------------------------------------------------------------
static const uint8_t kLumaQuant[64] = {
    16, 11, 10, 16, 24, 40, 51, 61,
    12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68,109,103, 77,
    24, 35, 55, 64, 81,104,113, 92,
    49, 64, 78, 87,103,121,120,101,
    72, 92, 95, 98,112,100,103, 99
};

static const uint8_t kChromaQuant[64] = {
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99
};

// Zigzag order
static const uint8_t kZigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

// ---------------------------------------------------------------------------
// Standard JPEG Huffman tables (JPEG Annex K)
// ---------------------------------------------------------------------------

// DC Luma
static const uint8_t kDcLumaBits[17] = {
    0, 0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0
};
static const uint8_t kDcLumaVals[] = {
    0,1,2,3,4,5,6,7,8,9,10,11
};

// DC Chroma
static const uint8_t kDcChromaBits[17] = {
    0, 0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0
};
static const uint8_t kDcChromaVals[] = {
    0,1,2,3,4,5,6,7,8,9,10,11
};

// AC Luma
static const uint8_t kAcLumaBits[17] = {
    0, 0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d
};
static const uint8_t kAcLumaVals[] = {
    0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,
    0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,
    0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,
    0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,
    0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,
    0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
    0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,
    0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,
    0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,
    0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,
    0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,
    0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
    0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,
    0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,
    0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,
    0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,
    0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,
    0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
    0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,
    0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
    0xf9,0xfa
};

// AC Chroma
static const uint8_t kAcChromaBits[17] = {
    0, 0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77
};
static const uint8_t kAcChromaVals[] = {
    0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,
    0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,
    0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,
    0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,
    0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,
    0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
    0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,
    0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
    0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,
    0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,
    0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,
    0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
    0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,
    0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,
    0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,
    0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,
    0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,
    0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
    0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,
    0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
    0xf9,0xfa
};

// ---------------------------------------------------------------------------
// Huffman code table (built from BITS/VALS arrays)
// ---------------------------------------------------------------------------
struct HuffTable {
    uint16_t code[256];
    uint8_t  size[256];
    // val_to_code[symbol] = {code, bits}
    void build(const uint8_t bits[17], const uint8_t* vals, int nvals) {
        std::memset(code, 0, sizeof(code));
        std::memset(size, 0, sizeof(size));
        uint32_t c = 0;
        int idx = 0;
        for (int l = 1; l <= 16; ++l) {
            for (int k = 0; k < bits[l]; ++k, ++idx) {
                if (idx < nvals) {
                    code[vals[idx]] = static_cast<uint16_t>(c);
                    size[vals[idx]] = static_cast<uint8_t>(l);
                }
                ++c;
            }
            c <<= 1;
        }
    }
};

// ---------------------------------------------------------------------------
// Bit writer
// ---------------------------------------------------------------------------
struct BitWriter {
    std::vector<uint8_t>& out;
    uint32_t buf = 0;
    int      bits = 0;

    explicit BitWriter(std::vector<uint8_t>& o) : out(o) {}

    void write(uint32_t code, int nbits) {
        buf = (buf << nbits) | (code & ((1u << nbits) - 1));
        bits += nbits;
        while (bits >= 8) {
            bits -= 8;
            uint8_t byte = static_cast<uint8_t>(buf >> bits);
            out.push_back(byte);
            if (byte == 0xFF) out.push_back(0x00); // byte stuffing
        }
    }

    void flush() {
        if (bits > 0) {
            uint8_t byte = static_cast<uint8_t>(buf << (8 - bits));
            out.push_back(byte);
            if (byte == 0xFF) out.push_back(0x00);
            bits = 0;
            buf  = 0;
        }
    }
};

// ---------------------------------------------------------------------------
// AAN fast DCT (Loeffler et al., 11 multiplies 29 adds per 1D pass)
// Applied in-place on 8 floats.
// ---------------------------------------------------------------------------
// Clean reference 1D DCT-II:
static void dct1d_ref(float d[8]) {
    float out[8];
    for (int k = 0; k < 8; ++k) {
        float s = 0.0f;
        for (int n = 0; n < 8; ++n)
            s += d[n] * std::cos((2*n+1)*k*3.14159265358979f/16.0f);
        out[k] = s;
    }
    for (int k = 0; k < 8; ++k) d[k] = out[k];
}

// 2D 8x8 DCT using separable 1D passes
static void dct2d(float block[64]) {
    // Row passes
    for (int y = 0; y < 8; ++y)
        dct1d_ref(block + y*8);
    // Column passes
    float col[8];
    for (int x = 0; x < 8; ++x) {
        for (int y=0;y<8;y++) col[y] = block[y*8+x];
        dct1d_ref(col);
        for (int y=0;y<8;y++) block[y*8+x] = col[y];
    }
    // Scaling: JPEG DCT normalization
    // DC: * (1/8), others: * (1/(4*sqrt(2))) for one axis, (1/4) for other
    for (int v = 0; v < 8; ++v) {
        for (int u = 0; u < 8; ++u) {
            float cu = (u == 0) ? 0.7071067812f : 1.0f;
            float cv = (v == 0) ? 0.7071067812f : 1.0f;
            block[v*8+u] *= cu * cv * 0.25f;
        }
    }
}

// ---------------------------------------------------------------------------
// Quality → quantization table scaling (IJG formula)
// ---------------------------------------------------------------------------
static void make_quant_table(const uint8_t base[64], int quality, uint8_t out[64]) {
    int scale;
    if (quality <= 0) quality = 1;
    if (quality > 100) quality = 100;
    if (quality < 50) scale = 5000 / quality;
    else              scale = 200 - quality * 2;

    for (int i = 0; i < 64; ++i) {
        int q = (base[i] * scale + 50) / 100;
        if (q < 1)   q = 1;
        if (q > 255) q = 255;
        out[i] = static_cast<uint8_t>(q);
    }
}

// ---------------------------------------------------------------------------
// JFIF marker writing helpers
// ---------------------------------------------------------------------------
static void write_u8(std::vector<uint8_t>& v, uint8_t x)  { v.push_back(x); }
static void write_u16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x & 0xFF));
}
static void write_marker(std::vector<uint8_t>& v, uint8_t marker) {
    write_u8(v, 0xFF);
    write_u8(v, marker);
}

static void write_APP0(std::vector<uint8_t>& v) {
    write_marker(v, 0xE0);
    write_u16(v, 16);  // length
    v.insert(v.end(), {'J','F','I','F',0}); // identifier
    write_u8(v, 1); write_u8(v, 1); // version 1.1
    write_u8(v, 0); // pixel aspect ratio: no units
    write_u16(v, 1); write_u16(v, 1); // Xdensity, Ydensity
    write_u8(v, 0); write_u8(v, 0); // thumbnail WxH
}

static void write_DQT(std::vector<uint8_t>& v, const uint8_t q[64], int id) {
    write_marker(v, 0xDB);
    write_u16(v, 2 + 1 + 64); // length
    write_u8(v, static_cast<uint8_t>(id)); // 0=luma, 1=chroma, precision=0 (8-bit)
    // Write in zigzag order
    for (int i = 0; i < 64; ++i)
        write_u8(v, q[kZigzag[i]]);
}

static void write_SOF0(std::vector<uint8_t>& v, int width, int height) {
    write_marker(v, 0xC0);
    write_u16(v, 17); // length
    write_u8(v, 8);   // precision
    write_u16(v, static_cast<uint16_t>(height));
    write_u16(v, static_cast<uint16_t>(width));
    write_u8(v, 3);   // 3 components
    // Y: id=1, H=2 V=2 (4:2:0 subsampling), quant table 0
    write_u8(v, 1); write_u8(v, 0x22); write_u8(v, 0);
    // Cb: id=2, H=1 V=1, quant table 1
    write_u8(v, 2); write_u8(v, 0x11); write_u8(v, 1);
    // Cr: id=3, H=1 V=1, quant table 1
    write_u8(v, 3); write_u8(v, 0x11); write_u8(v, 1);
}

static void write_DHT(std::vector<uint8_t>& v, const uint8_t bits[17],
                      const uint8_t* vals, int nvals, int tc, int th) {
    int total = 0;
    for (int i = 1; i <= 16; ++i) total += bits[i];
    write_marker(v, 0xC4);
    write_u16(v, static_cast<uint16_t>(2 + 1 + 16 + total));
    write_u8(v, static_cast<uint8_t>((tc << 4) | th));
    for (int i = 1; i <= 16; ++i) write_u8(v, bits[i]);
    for (int i = 0; i < nvals; ++i) write_u8(v, vals[i]);
}

static void write_SOS(std::vector<uint8_t>& v) {
    write_marker(v, 0xDA);
    write_u16(v, 12); // length
    write_u8(v, 3);   // 3 components
    write_u8(v, 1); write_u8(v, 0x00); // Y: DC table 0, AC table 0
    write_u8(v, 2); write_u8(v, 0x11); // Cb: DC table 1, AC table 1
    write_u8(v, 3); write_u8(v, 0x11); // Cr: DC table 1, AC table 1
    write_u8(v, 0); write_u8(v, 63); write_u8(v, 0); // Ss=0, Se=63, Ah/Al=0
}

// ---------------------------------------------------------------------------
// Number of bits needed to encode value v (JPEG VLC category)
// ---------------------------------------------------------------------------
static int num_bits(int v) {
    if (v < 0) v = -v;
    int n = 0;
    while (v) { v >>= 1; ++n; }
    return n;
}

// ---------------------------------------------------------------------------
// Encode one coefficient using the appropriate Huffman table
// ---------------------------------------------------------------------------
static void encode_dc(BitWriter& bw, const HuffTable& htdc, int diff) {
    int cat = num_bits(diff);
    bw.write(htdc.code[cat], htdc.size[cat]);
    if (cat > 0) {
        int val = (diff >= 0) ? diff : diff + (1 << cat) - 1;
        bw.write(val, cat);
    }
}

static void encode_ac(BitWriter& bw, const HuffTable& htac, int run, int level) {
    int cat = num_bits(level);
    int symbol = (run << 4) | cat;
    bw.write(htac.code[symbol], htac.size[symbol]);
    if (cat > 0) {
        int val = (level >= 0) ? level : level + (1 << cat) - 1;
        bw.write(val, cat);
    }
}

// ---------------------------------------------------------------------------
// Encode one 8x8 block
// Returns new prevDC
// ---------------------------------------------------------------------------
static int encode_block(BitWriter& bw, float block[64],
                         const uint8_t quant[64],
                         const HuffTable& htdc, const HuffTable& htac,
                         int prevDC) {
    // Quantize + zigzag
    int coeff[64];
    for (int i = 0; i < 64; ++i) {
        int zi = kZigzag[i];
        float q = static_cast<float>(quant[i]);
        float v = block[zi] / q;
        coeff[i] = static_cast<int>(v >= 0 ? v + 0.5f : v - 0.5f);
    }

    // DC coefficient
    int dcDiff = coeff[0] - prevDC;
    encode_dc(bw, htdc, dcDiff);

    // AC coefficients
    int run = 0;
    for (int i = 1; i < 64; ++i) {
        if (coeff[i] == 0) {
            if (i == 63) {
                // EOB
                bw.write(htac.code[0x00], htac.size[0x00]);
                break;
            }
            ++run;
            if (run == 16) {
                // ZRL (16 zeros)
                bw.write(htac.code[0xF0], htac.size[0xF0]);
                run = 0;
            }
        } else {
            encode_ac(bw, htac, run, coeff[i]);
            run = 0;
        }
    }

    return coeff[0];
}

// ---------------------------------------------------------------------------
// RGB → YCbCr (BT.601, full-range)
// ---------------------------------------------------------------------------
static void rgb_to_ycbcr(uint8_t r, uint8_t g, uint8_t b,
                          float& Y, float& Cb, float& Cr) {
    Y  =  0.299f  * r + 0.587f  * g + 0.114f  * b;
    Cb = -0.1687f * r - 0.3313f * g + 0.5f    * b + 128.0f;
    Cr =  0.5f    * r - 0.4187f * g - 0.0813f * b + 128.0f;
}

// ---------------------------------------------------------------------------
// Main encoder
// ---------------------------------------------------------------------------
std::vector<uint8_t> jpeg_encode(const Image& img, const JpegEncodeOptions& opts) {
    if (!img.valid() || img.width == 0 || img.height == 0)
        return {};

    // Build quantization tables
    uint8_t lumaQ[64], chromaQ[64];
    make_quant_table(kLumaQuant,   opts.quality, lumaQ);
    make_quant_table(kChromaQuant, opts.quality, chromaQ);

    // Build Huffman tables
    HuffTable htDCLuma, htDCChroma, htACLuma, htACChroma;
    htDCLuma.build(kDcLumaBits,   kDcLumaVals,   sizeof(kDcLumaVals));
    htDCChroma.build(kDcChromaBits, kDcChromaVals, sizeof(kDcChromaVals));
    htACLuma.build(kAcLumaBits,   kAcLumaVals,   sizeof(kAcLumaVals));
    htACChroma.build(kAcChromaBits, kAcChromaVals, sizeof(kAcChromaVals));

    std::vector<uint8_t> out;
    out.reserve(img.width * img.height / 4);

    // SOI
    write_marker(out, 0xD8);
    // APP0
    write_APP0(out);
    // DQT
    write_DQT(out, lumaQ,   0);
    write_DQT(out, chromaQ, 1);
    // SOF0
    write_SOF0(out, img.width, img.height);
    // DHT
    write_DHT(out, kDcLumaBits,   kDcLumaVals,   sizeof(kDcLumaVals),   0, 0);
    write_DHT(out, kDcChromaBits, kDcChromaVals, sizeof(kDcChromaVals), 0, 1);
    write_DHT(out, kAcLumaBits,   kAcLumaVals,   sizeof(kAcLumaVals),   1, 0);
    write_DHT(out, kAcChromaBits, kAcChromaVals, sizeof(kAcChromaVals), 1, 1);
    // SOS
    write_SOS(out);

    // Encode scan data
    // 4:2:0: each MCU is 16x16 pixels = 4 Y blocks + 1 Cb + 1 Cr
    int mcuW = (img.width  + 15) / 16;
    int mcuH = (img.height + 15) / 16;

    BitWriter bw(out);
    int prevDC[3] = {0, 0, 0};

    int ch = img.channels();

    for (int my = 0; my < mcuH; ++my) {
        for (int mx = 0; mx < mcuW; ++mx) {
            // Collect 16x16 pixel block
            float Y[4][64], Cb[64], Cr[64];
            // Accumulate Cb/Cr for 4:2:0 subsampling.
            // The 8x8 chroma block covers the 16x16 MCU: each chroma sample = avg of 2x2 pixels.
            // Use an 8x8 accumulator; each cell covers a 2x2 pixel region.
            float CbAcc[8][8] = {}, CrAcc[8][8] = {};

            for (int by = 0; by < 16; ++by) {
                int py = my * 16 + by;
                if (py >= img.height) py = img.height - 1;
                const uint8_t* row = img.row(py);

                for (int bx = 0; bx < 16; ++bx) {
                    int px = mx * 16 + bx;
                    if (px >= img.width) px = img.width - 1;

                    uint8_t r, g, b;
                    if (ch == 1) {
                        r = g = b = row[px];
                    } else if (ch >= 3) {
                        r = row[px*ch+0];
                        g = row[px*ch+1];
                        b = row[px*ch+2];
                    } else {
                        r = g = b = 128;
                    }

                    float fy, fcb, fcr;
                    rgb_to_ycbcr(r, g, b, fy, fcb, fcr);

                    // Y block: 4 8x8 blocks (top-left=0, top-right=1, bot-left=2, bot-right=3)
                    int blockIdx = (by/8)*2 + (bx/8);
                    int localY = (by%8)*8 + (bx%8);
                    Y[blockIdx][localY] = fy - 128.0f;

                    // 4:2:0 subsampling: each chroma sample covers a 2x2 pixel block
                    CbAcc[by/2][bx/2] += fcb;
                    CrAcc[by/2][bx/2] += fcr;
                }
            }

            // Build 8x8 Cb/Cr blocks from 2x2 pixel averages
            for (int v = 0; v < 8; ++v) {
                for (int u = 0; u < 8; ++u) {
                    Cb[v*8+u] = CbAcc[v][u] / 4.0f - 128.0f;
                    Cr[v*8+u] = CrAcc[v][u] / 4.0f - 128.0f;
                }
            }

            // DCT + encode 4 Y blocks
            for (int b = 0; b < 4; ++b) {
                dct2d(Y[b]);
                prevDC[0] = encode_block(bw, Y[b], lumaQ, htDCLuma, htACLuma, prevDC[0]);
            }
            // DCT + encode Cb
            dct2d(Cb);
            prevDC[1] = encode_block(bw, Cb, chromaQ, htDCChroma, htACChroma, prevDC[1]);
            // DCT + encode Cr
            dct2d(Cr);
            prevDC[2] = encode_block(bw, Cr, chromaQ, htDCChroma, htACChroma, prevDC[2]);
        }
    }

    bw.flush();

    // EOI
    write_marker(out, 0xD9);

    return out;
}

// ---------------------------------------------------------------------------
// JPEG decode — inverse DCT + YCbCr→RGB
// Handles baseline JPEG (SOF0) as encoded by jpeg_encode above.
// ---------------------------------------------------------------------------

// Clamp float to uint8 [0,255]
static inline uint8_t clamp8(float v) {
    if (v < 0.0f)   return 0;
    if (v > 255.0f) return 255;
    return static_cast<uint8_t>(v + 0.5f);
}

// Inverse 2D DCT.
// Input:  block[v*8+u] = F'[v][u] = (C(u)*C(v)/4) * Σ s * cos * cos
//   where C(0) = 1/sqrt(2), C(k>0) = 1.
// Output: block[y*8+x] = spatial samples s[y][x]
//
// IDCT: s(x,y) = Σ_u Σ_v (C(u)*C(v)/4) * F'(u,v) * cos((2x+1)uπ/16) * cos((2y+1)vπ/16)
static void idct2d_ref(float block[64]) {
    float out[64] = {};
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            float s = 0.0f;
            for (int v = 0; v < 8; ++v) {
                float cv = (v == 0) ? 0.7071067812f : 1.0f;
                for (int u = 0; u < 8; ++u) {
                    float cu = (u == 0) ? 0.7071067812f : 1.0f;
                    s += (cu * cv * 0.25f) * block[v*8+u]
                         * std::cos((2*x+1)*u*3.14159265358979f/16.0f)
                         * std::cos((2*y+1)*v*3.14159265358979f/16.0f);
                }
            }
            out[y*8+x] = s;
        }
    }
    for (int i = 0; i < 64; ++i) block[i] = out[i];
}

// ---------------------------------------------------------------------------
// Bit reader for scan data (MSB first, handles 0xFF00 byte stuffing)
// ---------------------------------------------------------------------------
struct ScanReader {
    const uint8_t* src;
    size_t         src_len;
    size_t         pos;
    uint32_t       buf;
    int            bits;

    void init(const uint8_t* s, size_t l, size_t start) {
        src = s; src_len = l; pos = start; buf = 0; bits = 0;
    }

    // Fill buffer with at least n bits
    void fill() {
        while (bits < 24 && pos < src_len) {
            uint8_t b = src[pos++];
            if (b == 0xFF) {
                if (pos >= src_len) break;
                uint8_t next = src[pos];
                if (next == 0x00) {
                    ++pos; // consume the stuffed zero; b = 0xFF is data
                } else {
                    // It's a marker (RST or EOI) — don't consume it, stop filling
                    --pos; // put back the 0xFF
                    break;
                }
            }
            buf = (buf << 8) | b;
            bits += 8;
        }
    }

    int read_bit() {
        if (bits == 0) fill();
        if (bits == 0) return 0;
        --bits;
        return (buf >> bits) & 1;
    }

    int read_bits(int n) {
        fill();
        if (n == 0) return 0;
        if (bits < n) { /* underflow — return zeros */ return 0; }
        bits -= n;
        return (buf >> bits) & ((1 << n) - 1);
    }
};

// ---------------------------------------------------------------------------
// Huffman decoder table (correct canonical JPEG Huffman per ITU T.81 Annex C)
// ---------------------------------------------------------------------------
struct HuffDec {
    uint8_t  huffval[256] = {};
    int      mincode[17]  = {};
    int      maxcode[17]  = {};
    int      valptr[17]   = {};
    bool     valid        = false;

    // Build from BITS[1..16] and HUFFVAL array
    void build(const uint8_t bits[17], const uint8_t* vals, int nvals) {
        // Copy huffval
        for (int i = 0; i < nvals; ++i) huffval[i] = vals[i];

        // Generate codes (ITU T.81 Figure C.1)
        int code = 0;
        int k    = 0;
        for (int l = 1; l <= 16; ++l) {
            if (bits[l] == 0) {
                mincode[l] = -1;
                maxcode[l] = -1;
                valptr[l]  = 0;
            } else {
                mincode[l] = code;
                maxcode[l] = code + bits[l] - 1;
                valptr[l]  = k;
                k += bits[l];
            }
            code = (code + bits[l]) << 1;
        }
        valid = true;
    }

    // Decode one symbol from ScanReader
    int decode(ScanReader& sr) const {
        int code = 0;
        for (int l = 1; l <= 16; ++l) {
            code = (code << 1) | sr.read_bit();
            if (mincode[l] != -1 && code >= mincode[l] && code <= maxcode[l]) {
                return huffval[valptr[l] + (code - mincode[l])];
            }
        }
        return -1; // error
    }
};

// Receive n bits and extend sign (ITU T.81 F.2.2.1)
static inline int receive_extend(ScanReader& sr, int n) {
    if (n == 0) return 0;
    int v = sr.read_bits(n);
    // If MSB is 0, value is negative: subtract (2^n - 1)
    if (v < (1 << (n - 1)))
        v += (-1 << n) + 1;
    return v;
}

// Decode one 8x8 block's worth of zigzag coefficients
static int decode_block(ScanReader& sr,
                         const HuffDec& htDC, const HuffDec& htAC,
                         const uint8_t quant[64],
                         int prevDC, float block[64]) {
    // Zero block
    for (int i = 0; i < 64; ++i) block[i] = 0.0f;

    // DC
    int sym = htDC.decode(sr);
    if (sym < 0) return prevDC;
    int dc = prevDC + receive_extend(sr, sym);

    // Dequantize DC (zigzag index 0 → natural index kZigzag[0] = 0)
    block[0] = (float)(dc * quant[0]);

    // AC
    for (int i = 1; i < 64; ) {
        int ac_sym = htAC.decode(sr);
        if (ac_sym < 0) break;
        if (ac_sym == 0x00) break; // EOB
        if (ac_sym == 0xF0) { i += 16; continue; } // ZRL

        int run = (ac_sym >> 4) & 0xF;
        int cat = ac_sym & 0xF;
        i += run;
        if (i >= 64) break;
        int coeff = receive_extend(sr, cat);
        // Dequantize: quant is in natural order, indexed by natural position kZigzag[i]
        block[kZigzag[i]] = (float)(coeff * quant[kZigzag[i]]);
        ++i;
    }

    return dc;
}

// ---------------------------------------------------------------------------
// JPEG decoder
// ---------------------------------------------------------------------------
Image jpeg_decode(const uint8_t* data, size_t len) {
    if (!data || len < 2) return {};
    if (data[0] != 0xFF || data[1] != 0xD8) return {};

    size_t pos = 2;
    uint8_t quant_table[2][64] = {};
    HuffDec dc_huff[2], ac_huff[2];
    int width = 0, height = 0;
    bool got_sof = false;

    auto read_u8  = [&]() -> uint8_t  { return (pos < len) ? data[pos++] : 0; };
    auto read_u16 = [&]() -> uint16_t {
        uint8_t h = read_u8(), l = read_u8();
        return (uint16_t)((h << 8) | l);
    };

    // Parse markers until SOS
    while (pos + 1 < len) {
        // Skip to next 0xFF marker
        while (pos < len && data[pos] != 0xFF) ++pos;
        if (pos + 1 >= len) break;
        ++pos; // skip 0xFF
        uint8_t marker = read_u8();

        if (marker == 0xD8) continue; // SOI (duplicate)
        if (marker == 0xD9) break;    // EOI
        if (marker == 0x00) continue; // stuffed byte
        if (marker >= 0xD0 && marker <= 0xD7) continue; // RST

        // All segments below have a 2-byte length field
        if (pos + 1 >= len) break;
        uint16_t seg_len = read_u16();
        if (seg_len < 2) { pos += (seg_len > 0 ? seg_len - 2 : 0); continue; }
        size_t seg_data_start = pos;  // points to first byte after length
        size_t seg_end = seg_data_start + (seg_len - 2);

        if (marker == 0xDB) { // DQT — Quantization Table
            size_t p = seg_data_start;
            while (p < seg_end) {
                uint8_t info = data[p++];
                int prec = (info >> 4) & 0xF;
                int id   = info & 0xF;
                if (id >= 2 || prec != 0) { p += 64 * (prec + 1); continue; }
                // Store in natural order (un-zigzag)
                for (int i = 0; i < 64; ++i)
                    quant_table[id][kZigzag[i]] = data[p++];
            }
        } else if (marker == 0xC0) { // SOF0 — Start of Frame (Baseline)
            /*precision =*/ read_u8();
            height = read_u16();
            width  = read_u16();
            // ncomp, component specs — skip
            got_sof = true;
        } else if (marker == 0xC4) { // DHT — Huffman Table
            size_t p = seg_data_start;
            while (p < seg_end) {
                uint8_t info = data[p++];
                int tc = (info >> 4) & 0xF; // 0=DC, 1=AC
                int th = info & 0xF;
                if (tc > 1 || th > 1) { p = seg_end; break; }

                uint8_t bits[17] = {};
                int total = 0;
                for (int i = 1; i <= 16; ++i) {
                    bits[i] = data[p++];
                    total += bits[i];
                }
                const uint8_t* vals = data + p;
                p += total;

                if (tc == 0) { dc_huff[th].build(bits, vals, total); }
                else         { ac_huff[th].build(bits, vals, total); }
            }
        } else if (marker == 0xDA) { // SOS — Start of Scan
            // Skip SOS header (to end of header, pos is already past length)
            // SOS header: ncomp(1) + comp_specs(2*ncomp) + Ss,Se,Ah/Al(3)
            // We trust seg_end points to start of compressed data
            pos = seg_end;
            // Compressed scan data follows
            break;
        }

        pos = seg_end;
    }

    if (!got_sof || width == 0 || height == 0) return {};

    // Decode scan
    ScanReader sr;
    sr.init(data, len, pos);

    Image img;
    img.width  = width;
    img.height = height;
    img.format = PixelFormat::RGB;
    img.pixels.assign(static_cast<size_t>(width) * height * 3, 0);

    int mcuW = (width  + 15) / 16;
    int mcuH = (height + 15) / 16;
    int prevDC[3] = {0, 0, 0};

    for (int my = 0; my < mcuH; ++my) {
        for (int mx = 0; mx < mcuW; ++mx) {
            float Yb[4][64] = {}, Cb[64] = {}, Cr[64] = {};

            // 4 Y blocks (2x2 arrangement within 16x16 MCU)
            for (int b = 0; b < 4; ++b) {
                prevDC[0] = decode_block(sr, dc_huff[0], ac_huff[0],
                                         quant_table[0], prevDC[0], Yb[b]);
                idct2d_ref(Yb[b]);
            }

            // 1 Cb block
            prevDC[1] = decode_block(sr, dc_huff[1], ac_huff[1],
                                     quant_table[1], prevDC[1], Cb);
            idct2d_ref(Cb);

            // 1 Cr block
            prevDC[2] = decode_block(sr, dc_huff[1], ac_huff[1],
                                     quant_table[1], prevDC[2], Cr);
            idct2d_ref(Cr);

            // Convert to RGB and write pixels
            for (int by = 0; by < 16; ++by) {
                int py = my * 16 + by;
                if (py >= height) continue;
                for (int bx = 0; bx < 16; ++bx) {
                    int px = mx * 16 + bx;
                    if (px >= width) continue;

                    // Which 8x8 Y block: top-left=0, top-right=1, bot-left=2, bot-right=3
                    int yb_idx = (by / 8) * 2 + (bx / 8);
                    int yi     = (by % 8) * 8 + (bx % 8);
                    // Cb/Cr: 8x8 block covers the full 16x16 MCU (2x downsampled)
                    int ci = (by / 2) * 8 + (bx / 2);

                    float fy  = Yb[yb_idx][yi] + 128.0f;
                    float fcb = Cb[ci];
                    float fcr = Cr[ci];

                    // YCbCr → RGB (BT.601 full-range inverse)
                    float rf = fy                      + 1.40200f * fcr;
                    float gf = fy - 0.34414f * fcb     - 0.71414f * fcr;
                    float bf = fy + 1.77200f * fcb;

                    size_t off = (static_cast<size_t>(py) * width + px) * 3;
                    img.pixels[off+0] = clamp8(rf);
                    img.pixels[off+1] = clamp8(gf);
                    img.pixels[off+2] = clamp8(bf);
                }
            }
        }
    }

    return img;
}

std::vector<uint8_t> jpeg_strip_exif(const uint8_t* data, size_t len) {
    return strip_jpeg_metadata(data, len);
}

} // namespace lp
