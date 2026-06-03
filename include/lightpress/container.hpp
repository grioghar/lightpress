#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lp {

struct ContainerStripOptions {
    bool strip_thumbnails = true;   // remove 'udta'/'meta' thumbnail tracks
    bool strip_cover_art  = true;   // remove iTunes cover-art atoms
    bool strip_extra_meta = true;   // remove 'mdta'/'(c)' atoms
    bool strip_audio      = false;  // remove audio track entirely (video-only messages)
};

// Parse an MP4/MOV container (MPEG-4 Part 12 atom tree), remove selected atoms,
// and return the rewritten container bytes. The encoded video/audio bitstreams
// are passed through unchanged -- no transcoding occurs.
// Returns empty vector on parse failure.
std::vector<std::uint8_t> strip_mp4_metadata(const std::uint8_t* data, std::size_t len,
                                             const ContainerStripOptions& opts = {});

// Returns total bytes of removable metadata in the container.
std::size_t mp4_metadata_bytes(const std::uint8_t* data, std::size_t len);

} // namespace lp
