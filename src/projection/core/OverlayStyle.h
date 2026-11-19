#pragma once
// Java Litematica overlay palette and per-type visibility (Colors + Visuals configuration groups).
#include <cstdint>

namespace lholo::projection {

struct OverlayStyle {
    // ABGR with alpha 0: the tessellator vertex-colour byte order; opacity is applied separately.
    std::uint32_t missing{0x00E6B333U}, extra{0x00E64CFFU}, wrongBlock{0x003333FFU}, wrongState{0x001090FFU};
    bool showMissing{true}, showExtra{true}, showWrongBlock{true}, showWrongState{true};
    bool operator==(OverlayStyle const&) const = default;
};

// "#RRGGBB" or "#AARRGGBB" -> ABGR rgb-only; invalid text returns fallback.
inline std::uint32_t overlayColorFromText(char const* text, std::uint32_t fallback) {
    if (!text || text[0] != '#') return fallback;
    std::uint32_t value = 0;
    int digits = 0;
    for (char const* c = text + 1; *c; ++c, ++digits) {
        std::uint32_t d;
        if (*c >= '0' && *c <= '9') d = *c - '0';
        else if (*c >= 'a' && *c <= 'f') d = *c - 'a' + 10;
        else if (*c >= 'A' && *c <= 'F') d = *c - 'A' + 10;
        else return fallback;
        value = value << 4 | d;
    }
    if (digits != 6 && digits != 8) return fallback;
    std::uint32_t const r = (value >> 16) & 0xFF, g = (value >> 8) & 0xFF, b = value & 0xFF;
    return b << 16 | g << 8 | r;
}

} // namespace lholo::projection
