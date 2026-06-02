// lightpress — MP4/MOV container metadata stripper
// Walks the MPEG-4 Part 12 atom tree, drops metadata atoms,
// rewrites size fields. No video/audio transcoding.

#include "lightpress/container.hpp"
#include <algorithm>
#include <cstring>
#include <vector>
#include <array>

namespace lp {

// ---------------------------------------------------------------------------
// Atom I/O helpers
// ---------------------------------------------------------------------------

static uint32_t read_u32_be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
          | ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static uint64_t read_u64_be(const uint8_t* p) {
    uint64_t hi = read_u32_be(p);
    uint64_t lo = read_u32_be(p + 4);
    return (hi << 32) | lo;
}

static void write_u32_be(uint8_t* p, uint32_t v) {
    p[0] = (v >> 24) & 0xFF; p[1] = (v >> 16) & 0xFF;
    p[2] = (v >>  8) & 0xFF; p[3] = (v >>  0) & 0xFF;
}

static void write_u32_be_vec(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((x >> 24) & 0xFF); v.push_back((x >> 16) & 0xFF);
    v.push_back((x >>  8) & 0xFF); v.push_back((x >>  0) & 0xFF);
}

static bool fourcc_eq(const uint8_t* p, const char* s) {
    return p[0] == (uint8_t)s[0] && p[1] == (uint8_t)s[1]
        && p[2] == (uint8_t)s[2] && p[3] == (uint8_t)s[3];
}

// ---------------------------------------------------------------------------
// Atom tree representation
// ---------------------------------------------------------------------------

struct Atom {
    uint64_t offset;   // absolute offset in source
    uint64_t size;     // total atom size (including header)
    uint8_t  type[4];
    bool     is_container; // has child atoms
    std::vector<Atom> children;
};

// Top-level atoms that contain child atoms
static const char* CONTAINER_ATOMS[] = {
    "moov", "trak", "mdia", "minf", "stbl", "edts", "dinf",
    "udta", "meta", "ilst", "clip", "matt", "kmat", "tref",
    "chap", "tmcd", "rtp ", "ipmc",
    nullptr
};

static bool is_container(const uint8_t type[4]) {
    for (int i = 0; CONTAINER_ATOMS[i]; ++i) {
        if (fourcc_eq(type, CONTAINER_ATOMS[i])) return true;
    }
    return false;
}

// Parse atom tree from [data, data+len)
// Returns list of top-level atoms
static std::vector<Atom> parse_atoms(const uint8_t* data, size_t len,
                                     uint64_t base_offset) {
    std::vector<Atom> result;
    uint64_t pos = 0;

    while (pos + 8 <= len) {
        Atom a;
        a.offset = base_offset + pos;

        uint32_t size32 = read_u32_be(data + pos);
        std::memcpy(a.type, data + pos + 4, 4);

        if (size32 == 0) {
            // Extends to end of file
            a.size = len - pos;
        } else if (size32 == 1) {
            // 64-bit size
            if (pos + 16 > len) break;
            a.size = read_u64_be(data + pos + 8);
        } else {
            a.size = size32;
        }

        if (a.size < 8 || pos + a.size > len + 1) {
            // Allow last atom to be truncated by 1 for EOF
            a.size = len - pos;
        }

        a.is_container = is_container(a.type);

        if (a.is_container && a.size >= 8) {
            // Parse children (skip the 8-byte header; 'meta' has extra 4-byte version/flags)
            uint64_t hdr = 8;
            if (size32 == 1) hdr = 16;
            // 'meta' atom has a 4-byte version/flags field before children
            if (fourcc_eq(a.type, "meta")) hdr += 4;
            if (hdr < a.size) {
                a.children = parse_atoms(data + pos + hdr, (size_t)(a.size - hdr),
                                         base_offset + pos + hdr);
            }
        }

        result.push_back(std::move(a));
        pos += a.size;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Metadata atom type checkers
// ---------------------------------------------------------------------------

static bool is_metadata_atom(const uint8_t type[4], const ContainerStripOptions& opts) {
    // udta — user data (contains thumbnails, chapter info, etc.)
    if (opts.strip_thumbnails && fourcc_eq(type, "udta")) return true;

    // Cover art: in ilst/(c)ovr
    if (opts.strip_cover_art) {
        // We handle ilst's children separately; here flag the (c)ovr child
        if (type[0] == 0xA9 && type[1] == 'o' && type[2] == 'v' && type[3] == 'r')
            return true;
        // Common iTunes cover atom names
        if (fourcc_eq(type, "covr")) return true;
    }

    // Extra metadata atoms
    if (opts.strip_extra_meta) {
        // Free space atoms
        if (fourcc_eq(type, "free")) return true;
        if (fourcc_eq(type, "skip")) return true;
        // Wide atom (only used as size extender)
        if (fourcc_eq(type, "wide")) return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Rewrite atom tree, dropping metadata atoms, returning new bytes
// ---------------------------------------------------------------------------

struct RewriteCtx {
    const uint8_t* src;
    size_t src_len;
    const ContainerStripOptions& opts;
};

// Forward declaration
static std::vector<uint8_t> rewrite_atom(const RewriteCtx& ctx, const Atom& a);

static std::vector<uint8_t> rewrite_children(const RewriteCtx& ctx,
                                              const std::vector<Atom>& children,
                                              const uint8_t parent_type[4]) {
    std::vector<uint8_t> out;
    for (const auto& child : children) {
        // Drop audio track if requested
        if (ctx.opts.strip_audio && fourcc_eq(child.type, "trak")) {
            // Check if this is an audio trak by scanning mdia→hdlr
            // Simple heuristic: if mdia/hdlr soun, skip
            bool is_audio = false;
            for (const auto& c1 : child.children) {
                if (fourcc_eq(c1.type, "mdia")) {
                    for (const auto& c2 : c1.children) {
                        if (fourcc_eq(c2.type, "hdlr")) {
                            // hdlr body: skip 8 bytes version/flags/pre_defined,
                            // then 4 bytes handler type
                            uint64_t off = c2.offset;
                            if (off + 12 + 8 <= ctx.src_len) {
                                if (fourcc_eq(ctx.src + off + 16, "soun"))
                                    is_audio = true;
                            }
                        }
                    }
                }
            }
            if (is_audio) continue;
        }

        // Drop cover art from ilst
        if (ctx.opts.strip_cover_art && fourcc_eq(parent_type, "ilst")) {
            if (fourcc_eq(child.type, "covr")) continue;
            // iTunes metadata atoms starting with 0xA9
            // Keep them unless they are cover art
        }

        if (is_metadata_atom(child.type, ctx.opts)) continue;

        auto bytes = rewrite_atom(ctx, child);
        out.insert(out.end(), bytes.begin(), bytes.end());
    }
    return out;
}

static std::vector<uint8_t> rewrite_atom(const RewriteCtx& ctx, const Atom& a) {
    std::vector<uint8_t> out;

    if (!a.is_container) {
        // Leaf atom: copy verbatim
        uint64_t off = a.offset;
        uint64_t sz  = a.size;
        if (off + sz <= ctx.src_len) {
            out.insert(out.end(), ctx.src + off, ctx.src + off + sz);
        }
        return out;
    }

    // Container: rewrite header + rewritten children
    // Figure out original header size
    uint32_t orig_size32 = read_u32_be(ctx.src + a.offset);
    uint64_t hdr_size = (orig_size32 == 1) ? 16 : 8;
    bool meta_extra = fourcc_eq(a.type, "meta");

    // Build new children
    std::vector<uint8_t> children_bytes =
        rewrite_children(ctx, a.children, a.type);

    // Build header
    // Compute new total size
    uint64_t extra = meta_extra ? 4 : 0;
    uint64_t new_size = hdr_size + extra + children_bytes.size();

    if (new_size <= 0xFFFFFFFF) {
        write_u32_be_vec(out, (uint32_t)new_size);
    } else {
        write_u32_be_vec(out, 1); // extended size
    }
    out.push_back(a.type[0]); out.push_back(a.type[1]);
    out.push_back(a.type[2]); out.push_back(a.type[3]);

    if (orig_size32 == 1) {
        // Write 64-bit size
        out.push_back((new_size >> 56) & 0xFF);
        out.push_back((new_size >> 48) & 0xFF);
        out.push_back((new_size >> 40) & 0xFF);
        out.push_back((new_size >> 32) & 0xFF);
        out.push_back((new_size >> 24) & 0xFF);
        out.push_back((new_size >> 16) & 0xFF);
        out.push_back((new_size >>  8) & 0xFF);
        out.push_back((new_size >>  0) & 0xFF);
    }

    // 'meta' version/flags
    if (meta_extra) {
        // Copy from source
        uint64_t vf_off = a.offset + hdr_size;
        if (vf_off + 4 <= ctx.src_len) {
            out.insert(out.end(), ctx.src + vf_off, ctx.src + vf_off + 4);
        } else {
            out.push_back(0); out.push_back(0); out.push_back(0); out.push_back(0);
        }
    }

    out.insert(out.end(), children_bytes.begin(), children_bytes.end());

    // Fix 32-bit size in header if it was stored as 32-bit
    if (orig_size32 != 1 && out.size() <= 0xFFFFFFFF) {
        write_u32_be(out.data(), (uint32_t)out.size());
    }

    return out;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::vector<uint8_t> strip_mp4_metadata(const uint8_t* data, size_t len,
                                          const ContainerStripOptions& opts) {
    if (!data || len < 8) return {};

    // Quick sanity: check first atom is ftyp or mdat or moov
    uint32_t first_type_u32 = read_u32_be(data + 4);
    (void)first_type_u32;

    auto atoms = parse_atoms(data, len, 0);
    if (atoms.empty()) return {};

    RewriteCtx ctx{ data, len, opts };
    std::vector<uint8_t> out;
    out.reserve(len);

    for (const auto& a : atoms) {
        if (is_metadata_atom(a.type, opts)) continue;
        auto bytes = rewrite_atom(ctx, a);
        out.insert(out.end(), bytes.begin(), bytes.end());
    }

    return out;
}

size_t mp4_metadata_bytes(const uint8_t* data, size_t len) {
    if (!data || len < 8) return 0;
    ContainerStripOptions opts;
    auto stripped = strip_mp4_metadata(data, len, opts);
    if (stripped.empty() || stripped.size() >= len) return 0;
    return len - stripped.size();
}

} // namespace lp
