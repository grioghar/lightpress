#pragma once
#include <cstdint>
#include <vector>

namespace lp {

// Strip all APP1 (EXIF), APP2 (ICC/Flashpix), APP13 (IPTC) markers from a JPEG.
// Preserves APP0 (JFIF) and all pixel data (SOF/SOS/DHT/DQT segments).
// Returns the stripped JPEG bytes. Input must be a valid JPEG (starts with FF D8).
std::vector<uint8_t> strip_jpeg_metadata(const uint8_t* data, size_t len);

// How many bytes of metadata were removed.
size_t jpeg_metadata_bytes(const uint8_t* data, size_t len);

} // namespace lp
