#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Access to the grioghar/ffmpeg subprocess configured at build time.
// All functions are no-ops (returning false / empty) when ffmpeg is unavailable.
//
// lightpress itself has ZERO link-time dependency on ffmpeg — the binary is
// invoked as a subprocess.  This keeps the lightpress ABI stable regardless
// of the ffmpeg version and avoids any LGPL link-constraint complications.

namespace lp {

// Returns the ffmpeg binary path resolved at CMake configure time.
// Empty string when ffmpeg was not found.
inline std::string ffmpeg_path() {
#ifdef LP_FFMPEG_PATH
    return LP_FFMPEG_PATH;
#else
    return "";
#endif
}

// Returns true if an ffmpeg binary is configured and executable.
bool ffmpeg_available();

// Transcode options for the subprocess call.
struct TranscodeOptions {
    // Video
    std::string video_codec   = "libx265";  // h264, libx265, copy
    int         video_crf     = 28;          // 0=lossless, 51=worst
    std::string video_preset  = "fast";

    // Audio
    std::string audio_codec   = "aac";      // aac, mp3, copy, none
    int         audio_bitrate = 128;         // kbps

    // Container
    std::string format        = "";          // "" = infer from output path

    // Limits
    int         timeout_secs  = 300;         // kill ffmpeg after this
};

// Transcode `input_path` → `output_path` using the configured ffmpeg.
// Returns true on success.  Appends ffmpeg stderr to `error_out` on failure.
bool transcode(const std::string& input_path,
               const std::string& output_path,
               const TranscodeOptions& opts = {},
               std::string* error_out = nullptr);

// Run ffprobe (if available) to return duration_seconds and a codec summary.
struct MediaInfo {
    double      duration_secs = 0;
    std::string video_codec;
    int         width = 0, height = 0;
    std::string audio_codec;
    int         audio_bitrate_kbps = 0;
    int64_t     file_bytes = 0;
    bool        valid = false;
};
MediaInfo probe(const std::string& path);

} // namespace lp
