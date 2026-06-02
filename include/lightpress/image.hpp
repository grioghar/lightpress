#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace lp {

enum class PixelFormat { RGB, RGBA, Grayscale };

struct Image {
    std::vector<uint8_t> pixels;  // row-major, tightly packed
    int width  = 0;
    int height = 0;
    PixelFormat format = PixelFormat::RGB;

    int channels() const {
        switch (format) {
            case PixelFormat::RGB:       return 3;
            case PixelFormat::RGBA:      return 4;
            case PixelFormat::Grayscale: return 1;
        }
        return 3;
    }

    int stride() const { return width * channels(); }

    uint8_t* row(int y) {
        return pixels.data() + y * stride();
    }

    const uint8_t* row(int y) const {
        return pixels.data() + y * stride();
    }

    bool valid() const {
        if (width <= 0 || height <= 0) return false;
        return pixels.size() == static_cast<size_t>(width) * height * channels();
    }
};

// Read a JPEG or PNG file into an Image (basic file reader for test harness only).
Image read_file(const std::string& path);

} // namespace lp
