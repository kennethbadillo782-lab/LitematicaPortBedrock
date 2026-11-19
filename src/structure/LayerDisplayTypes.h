// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Layer types shared by runtime code. Settings and protocols serialize stable integer values.

#pragma once

#include <algorithm>

namespace lholo::structure {

enum class LayerAxis : int {
    Y        = 0,
    X        = 1,
    Material = 2,
    Z        = 3, // Append: preserve existing configuration values.
};

enum class LayerDisplayMode : int {
    All           = 0,
    Single        = 1,
    UpToCurrent   = 2,
    FromCurrent   = 3,
    Range         = 4, // Litematica layer range: start..end, both inclusive.
};

// A range is stored in the single persisted/serialized layer value: start in the low 15 bits, end in the next 15.
// Every other mode keeps just the start, so older saved values and all change detection keep working unchanged.
inline constexpr int kLayerRangeMask = 0x7FFF;
[[nodiscard]] constexpr int layerRangeStart(int packed) { return packed & kLayerRangeMask; }
[[nodiscard]] constexpr int layerRangeEnd(int packed) { return (packed >> 15) & kLayerRangeMask; }
[[nodiscard]] constexpr int packLayerRange(int start, int end) {
    start = start < 0 ? 0 : start > kLayerRangeMask ? kLayerRangeMask : start;
    end = end < start ? start : end > kLayerRangeMask ? kLayerRangeMask : end;
    return start | (end << 15);
}
// Normalises a layer value for a mode: clamps to [0, maximum] and keeps or drops the range end.
[[nodiscard]] constexpr int clampLayerValue(int packed, bool range, int maximum) {
    if (maximum < 0) maximum = 0;
    if (maximum > kLayerRangeMask) maximum = kLayerRangeMask;
    int start = layerRangeStart(packed);
    if (start > maximum) start = maximum;
    if (!range) return start;
    int end = layerRangeEnd(packed);
    if (end < start) end = maximum; // a value without a range end covers everything from the start
    if (end > maximum) end = maximum;
    return start | (end << 15);
}

// One coordinate policy for rendering, correction, materials and placement.
[[nodiscard]] constexpr int layerCoordinate(LayerAxis axis, int x, int y, int z) {
    return axis == LayerAxis::X ? x : axis == LayerAxis::Z ? z : y;
}

[[nodiscard]] constexpr int toInt(LayerAxis value) {
    return static_cast<int>(value);
}

[[nodiscard]] constexpr int toInt(LayerDisplayMode value) {
    return static_cast<int>(value);
}

[[nodiscard]] constexpr LayerAxis layerAxisFromInt(int value) {
    return static_cast<LayerAxis>(std::clamp(value, 0, 3));
}

[[nodiscard]] constexpr LayerDisplayMode layerDisplayModeFromInt(int value) {
    return static_cast<LayerDisplayMode>(std::clamp(value, 0, 4));
}

} // namespace lholo::structure
