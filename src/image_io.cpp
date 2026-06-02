// lightpress — image file reader (test harness only)
// Dispatches to jpeg_decode / png_decode based on file extension/magic bytes.

#include "lightpress/image.hpp"
#include "lightpress/jpeg.hpp"
#include "lightpress/png.hpp"
#include <fstream>
#include <stdexcept>
#include <vector>

namespace lp {

Image read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open: " + path);

    f.seekg(0, std::ios::end);
    size_t sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> buf(sz);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz));

    if (sz >= 2 && buf[0] == 0xFF && buf[1] == 0xD8)
        return jpeg_decode(buf.data(), sz);

    if (sz >= 8) {
        const uint8_t sig[8] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
        bool is_png = true;
        for (int i = 0; i < 8; ++i) if (buf[i] != sig[i]) { is_png = false; break; }
        if (is_png) return png_decode(buf.data(), sz);
    }

    throw std::runtime_error("Unknown file format: " + path);
}

} // namespace lp
