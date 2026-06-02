// lightpress — JPEG metadata stripper
// Scans the JFIF marker sequence and drops APP1 (EXIF), APP2 (ICC/Flashpix),
// APP13 (IPTC) markers. Preserves APP0, all coding segments.

#include "lightpress/exif.hpp"
#include <cstring>

namespace lp {

static bool is_jpeg(const uint8_t* data, size_t len) {
    return len >= 2 && data[0] == 0xFF && data[1] == 0xD8;
}

static bool should_strip_marker(uint8_t marker) {
    // APP1 = 0xE1 (EXIF / XMP)
    // APP2 = 0xE2 (ICC profile / Flashpix)
    // APP13 = 0xED (IPTC / Photoshop)
    // APP14 = 0xEE (Adobe)
    return marker == 0xE1 || marker == 0xE2 || marker == 0xED;
}

static size_t count_metadata_bytes(const uint8_t* data, size_t len) {
    if (!is_jpeg(data, len)) return 0;
    size_t pos = 2;
    size_t total = 0;

    while (pos + 1 < len) {
        if (data[pos] != 0xFF) { ++pos; continue; }
        ++pos;
        if (pos >= len) break;
        uint8_t marker = data[pos++];

        // Markers without a length field
        if (marker == 0xD8 || marker == 0xD9) continue;
        if (marker >= 0xD0 && marker <= 0xD7) continue; // RST markers
        if (marker == 0x00) continue; // stuffed byte

        if (pos + 1 >= len) break;
        uint16_t seg_len = (uint16_t)((data[pos] << 8) | data[pos+1]);
        if (seg_len < 2) { pos += 2; continue; }

        if (should_strip_marker(marker)) {
            total += 2 + seg_len; // marker(2) + length field + payload
        }

        if (marker == 0xDA) break; // SOS: compressed data follows, stop scanning

        pos += seg_len;
    }
    return total;
}

std::vector<uint8_t> strip_jpeg_metadata(const uint8_t* data, size_t len) {
    if (!is_jpeg(data, len)) return {};

    std::vector<uint8_t> out;
    out.reserve(len);

    // Copy SOI
    out.push_back(0xFF);
    out.push_back(0xD8);

    size_t pos = 2;

    while (pos + 1 < len) {
        if (data[pos] != 0xFF) {
            // Inside scan data (after SOS) — copy raw
            out.push_back(data[pos++]);
            continue;
        }
        ++pos;
        if (pos >= len) break;
        uint8_t marker = data[pos++];

        // SOI / EOI / RST
        if (marker == 0xD8) {
            out.push_back(0xFF); out.push_back(0xD8);
            continue;
        }
        if (marker == 0xD9) {
            out.push_back(0xFF); out.push_back(0xD9);
            break;
        }
        if (marker >= 0xD0 && marker <= 0xD7) {
            out.push_back(0xFF); out.push_back(marker);
            continue;
        }
        if (marker == 0x00) {
            // Stuffed byte inside scan — copy
            out.push_back(0xFF); out.push_back(0x00);
            continue;
        }

        if (pos + 1 >= len) break;
        uint16_t seg_len = (uint16_t)((data[pos] << 8) | data[pos+1]);
        if (seg_len < 2) { pos += 2; continue; }
        size_t payload_start = pos + 2;
        size_t payload_len   = seg_len - 2;

        if (should_strip_marker(marker)) {
            // Drop this segment
            pos += seg_len;
            continue;
        }

        // Copy this segment
        out.push_back(0xFF);
        out.push_back(marker);
        out.push_back(data[pos]);   // len high
        out.push_back(data[pos+1]); // len low

        if (payload_start + payload_len <= len) {
            out.insert(out.end(),
                       data + payload_start,
                       data + payload_start + payload_len);
        }
        pos += seg_len;

        // After SOS, the rest is scan data — copy through to EOI
        if (marker == 0xDA) {
            while (pos < len) {
                out.push_back(data[pos]);
                if (data[pos] == 0xFF && pos+1 < len && data[pos+1] == 0xD9) {
                    out.push_back(data[pos+1]);
                    pos += 2;
                    goto done;
                }
                ++pos;
            }
            break;
        }
    }
done:
    return out;
}

size_t jpeg_metadata_bytes(const uint8_t* data, size_t len) {
    return count_metadata_bytes(data, len);
}

} // namespace lp
