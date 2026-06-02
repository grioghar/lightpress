// lightpress — ffmpeg comparison benchmark
// SKIPPED if ffmpeg is not found on PATH.
// Generates a 128x128 gradient, encodes with lightpress and ffmpeg,
// compares file sizes and SSIM.

#include "lightpress/jpeg.hpp"
#include "lightpress/png.hpp"
#include "ssim.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define popen  _popen
#define pclose _pclose
#endif

using namespace lp;

static bool ffmpeg_available() {
    // Try running ffmpeg -version and check return code
    FILE* f = popen("ffmpeg -version 2>&1", "r");
    if (!f) return false;
    char buf[256] = {};
    bool found = false;
    while (fgets(buf, sizeof(buf), f)) {
        if (std::strstr(buf, "ffmpeg version")) { found = true; break; }
    }
    pclose(f);
    return found;
}

static Image make_gradient(int w, int h) {
    Image img;
    img.width = w; img.height = h; img.format = PixelFormat::RGB;
    img.pixels.resize(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
        uint8_t* row = img.row(y);
        for (int x = 0; x < w; ++x) {
            row[x*3+0] = (uint8_t)(x * 255 / (w-1));
            row[x*3+1] = (uint8_t)(y * 255 / (h-1));
            row[x*3+2] = (uint8_t)((x + y) * 128 / (w+h-2));
        }
    }
    return img;
}

static void write_ppm(const std::string& path, const Image& img) {
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << img.width << " " << img.height << "\n255\n";
    f.write(reinterpret_cast<const char*>(img.pixels.data()),
            static_cast<std::streamsize>(img.pixels.size()));
}

static std::vector<uint8_t> read_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    f.seekg(0, std::ios::end);
    size_t sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> buf(sz);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz));
    return buf;
}

int main() {
    printf("=== ffmpeg benchmark ===\n");

    if (!ffmpeg_available()) {
        printf("SKIP: ffmpeg not found on PATH.\n");
        return 0;
    }
    printf("ffmpeg found. Running comparison...\n");

    Image src = make_gradient(128, 128);
    size_t raw_bytes = static_cast<size_t>(src.width) * src.height * src.channels();

    // Temp file paths
    const char* ppm_path  = "lp_bench_input.ppm";
    const char* ffmpeg_out = "lp_bench_ffmpeg.jpg";
    const char* lp_out    = "lp_bench_lp.jpg";

    write_ppm(ppm_path, src);

    // --- lightpress encode ---
    auto t0 = std::chrono::high_resolution_clock::now();
    JpegEncodeOptions opts; opts.quality = 85;
    auto lp_bytes = jpeg_encode(src, opts);
    auto t1 = std::chrono::high_resolution_clock::now();
    double lp_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    {
        std::ofstream f(lp_out, std::ios::binary);
        f.write(reinterpret_cast<const char*>(lp_bytes.data()),
                static_cast<std::streamsize>(lp_bytes.size()));
    }

    // --- ffmpeg encode (quality ~85 ≈ -q:v 2) ---
    std::string cmd = std::string("ffmpeg -y -i ") + ppm_path
                    + " -q:v 2 " + ffmpeg_out + " 2>&1";
    auto t2 = std::chrono::high_resolution_clock::now();
    int rc = std::system(cmd.c_str());
    auto t3 = std::chrono::high_resolution_clock::now();
    double ffmpeg_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();

    if (rc != 0) {
        printf("SKIP: ffmpeg encode failed (rc=%d)\n", rc);
        std::remove(ppm_path);
        return 0;
    }

    auto ffmpeg_data = read_bytes(ffmpeg_out);
    size_t ffmpeg_sz = ffmpeg_data.size();
    size_t lp_sz     = lp_bytes.size();

    // Decode both for SSIM
    Image lp_dec     = jpeg_decode(lp_bytes.data(), lp_bytes.size());
    Image ffmpeg_dec = jpeg_decode(ffmpeg_data.data(), ffmpeg_data.size());

    double lp_ssim     = (lp_dec.valid())     ? ssim(src, lp_dec)     : 0.0;
    double ffmpeg_ssim = (ffmpeg_dec.valid())  ? ssim(src, ffmpeg_dec) : 0.0;

    printf("\n%-20s %10s %10s %12s\n", "Encoder", "Size(B)", "SSIM", "Time(ms)");
    printf("%-20s %10s %10s %12s\n", "------", "------", "----", "-------");
    printf("%-20s %10zu %10.4f %12.2f\n", "raw (PPM)", raw_bytes,  1.0,        0.0);
    printf("%-20s %10zu %10.4f %12.2f\n", "lightpress",lp_sz,      lp_ssim,    lp_ms);
    printf("%-20s %10zu %10.4f %12.2f\n", "ffmpeg",    ffmpeg_sz,  ffmpeg_ssim,ffmpeg_ms);

    double size_ratio = (ffmpeg_sz > 0) ? (double)lp_sz / (double)ffmpeg_sz : 0.0;
    printf("\nlightpress/ffmpeg size ratio: %.2fx (within 15%%: %s)\n",
           size_ratio, (size_ratio <= 1.15 && size_ratio >= 0.85) ? "YES" : "NO");

    // Assertion: lightpress should be within 15% of ffmpeg's output size
    if (size_ratio > 1.15) {
        printf("WARNING: lightpress output is more than 15%% larger than ffmpeg.\n");
    }

    // Cleanup
    std::remove(ppm_path);
    std::remove(lp_out);
    std::remove(ffmpeg_out);

    return 0;
}
