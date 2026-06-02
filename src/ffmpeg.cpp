#include "lightpress/ffmpeg.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#  define popen  _popen
#  define pclose _pclose
#endif

namespace lp {

bool ffmpeg_available() {
    const std::string path = ffmpeg_path();
    if (path.empty()) return false;
    // Quick existence / executable check: run "ffmpeg -version" and check exit
    std::string cmd = "\"" + path + "\" -version > /dev/null 2>&1";
#ifdef _WIN32
    cmd = "\"" + path + "\" -version > nul 2>&1";
#endif
    return std::system(cmd.c_str()) == 0;
}

bool transcode(const std::string& input_path,
               const std::string& output_path,
               const TranscodeOptions& opts,
               std::string* error_out) {
    const std::string fp = ffmpeg_path();
    if (fp.empty()) {
        if (error_out) *error_out = "ffmpeg not configured";
        return false;
    }

    std::ostringstream cmd;
    cmd << "\"" << fp << "\""
        << " -y"                          // overwrite output without asking
        << " -i \"" << input_path << "\""
        << " -c:v " << opts.video_codec;

    if (opts.video_codec != "copy") {
        cmd << " -crf " << opts.video_crf
            << " -preset " << opts.video_preset;
    }

    if (opts.audio_codec == "none") {
        cmd << " -an";
    } else {
        cmd << " -c:a " << opts.audio_codec;
        if (opts.audio_codec != "copy")
            cmd << " -b:a " << opts.audio_bitrate << "k";
    }

    if (!opts.format.empty())
        cmd << " -f " << opts.format;

    cmd << " \"" << output_path << "\""
        << " 2>&1";

    // Run and capture stderr (merged into stdout via 2>&1)
    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        if (error_out) *error_out = "popen failed";
        return false;
    }

    std::string output;
    std::array<char, 256> buf{};
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe))
        output += buf.data();

    int rc = pclose(pipe);
    if (rc != 0 && error_out)
        *error_out = output;
    return rc == 0;
}

MediaInfo probe(const std::string& path) {
    MediaInfo info;
    // Find ffprobe alongside ffmpeg
    std::string fp = ffmpeg_path();
    if (fp.empty()) return info;
    // Replace "ffmpeg" with "ffprobe" in the path
    const std::string needle = "ffmpeg";
    auto pos = fp.rfind(needle);
    if (pos != std::string::npos) fp.replace(pos, needle.size(), "ffprobe");

    std::ostringstream cmd;
    cmd << "\"" << fp << "\""
        << " -v quiet -print_format json -show_format -show_streams"
        << " \"" << path << "\" 2>&1";

    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) return info;

    std::string json;
    std::array<char, 512> buf{};
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe))
        json += buf.data();
    pclose(pipe);

    // Minimal JSON extraction (no external JSON library)
    auto extract = [&](const std::string& key) -> std::string {
        const std::string search = "\"" + key + "\": \"";
        auto p = json.find(search);
        if (p == std::string::npos) {
            // Try numeric form
            const std::string ns = "\"" + key + "\": ";
            p = json.find(ns);
            if (p == std::string::npos) return {};
            p += ns.size();
            auto e = json.find_first_of(",}\n", p);
            return json.substr(p, e - p);
        }
        p += search.size();
        auto e = json.find('"', p);
        return json.substr(p, e - p);
    };

    info.duration_secs   = std::stod(extract("duration").empty() ? "0" : extract("duration"));
    info.video_codec     = extract("codec_name");
    info.audio_codec     = extract("codec_name");  // first occurrence = video, second = audio
    const std::string w  = extract("width");
    const std::string h  = extract("height");
    if (!w.empty()) info.width  = std::stoi(w);
    if (!h.empty()) info.height = std::stoi(h);
    info.file_bytes      = std::stoll(extract("size").empty() ? "0" : extract("size"));
    info.valid           = !json.empty();
    return info;
}

} // namespace lp
