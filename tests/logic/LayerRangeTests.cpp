#include "structure/LayerDisplayTypes.h"
#include "structure/LayerVisibility.h"

#include <cstdio>

int runLayerRangeTests() {
    using namespace lholo::structure;
    int failures{}, checks{};
    auto check = [&](bool value, char const* message) {
        ++checks;
        if (!value) { ++failures; std::fprintf(stderr, "LayerRangeTests: %s\n", message); }
    };

    // Packing keeps start in the low bits and the end in the next ones; other modes are unaffected.
    check(layerRangeStart(packLayerRange(3, 9)) == 3, "start");
    check(layerRangeEnd(packLayerRange(3, 9)) == 9, "end");
    check(packLayerRange(5, 2) == packLayerRange(5, 5), "end below start becomes the start");
    check(packLayerRange(-4, 7) == packLayerRange(0, 7), "negative start clamps to 0");
    check(layerRangeStart(40000) == (40000 & kLayerRangeMask), "plain values keep the start in the low bits");
    check(packLayerRange(0, 100000) == packLayerRange(0, kLayerRangeMask), "end clamps to the representable maximum");
    check(packLayerRange(2, 6) > 0, "packed range stays a positive int");
    check(packLayerRange(kLayerRangeMask, kLayerRangeMask) == (kLayerRangeMask | (kLayerRangeMask << 15)), "largest range");

    // Normalisation by mode.
    check(clampLayerValue(7, false, 4) == 4, "non-range clamps to the maximum");
    check(clampLayerValue(packLayerRange(2, 6), false, 10) == 2, "leaving range mode drops the end");
    check(clampLayerValue(packLayerRange(2, 6), true, 10) == packLayerRange(2, 6), "range inside bounds is kept");
    check(clampLayerValue(packLayerRange(2, 60), true, 10) == packLayerRange(2, 10), "range end clamps to the maximum");
    check(clampLayerValue(packLayerRange(20, 60), true, 10) == packLayerRange(10, 10), "range start clamps to the maximum");
    check(clampLayerValue(3, true, 8) == packLayerRange(3, 8), "a value without an end covers start..last layer");

    // Visibility: both ends are inclusive, on every axis.
    LayerSettings range{LayerAxis::Y, LayerDisplayMode::Range, packLayerRange(2, 4)};
    for (int y = 0; y < 8; ++y) check(layerCellVisible(range, 0, y) == (y >= 2 && y <= 4), "Y range");
    range.axis = LayerAxis::X;
    for (int x = 0; x < 8; ++x) check(layerCellVisible(range, x, 0) == (x >= 2 && x <= 4), "X range");
    range.axis = LayerAxis::Z;
    for (int z = 0; z < 8; ++z) check(layerCellVisible(range, 0, 0, -1, -1, z) == (z >= 2 && z <= 4), "Z range");
    // A single-layer range equals the single mode.
    LayerSettings one{LayerAxis::Y, LayerDisplayMode::Range, packLayerRange(3, 3)};
    for (int y = 0; y < 8; ++y) check(layerCellVisible(one, 0, y) == (y == 3), "one-layer range");
    // Material axis: either the block or the liquid material may fall inside the range.
    LayerSettings materials{LayerAxis::Material, LayerDisplayMode::Range, packLayerRange(1, 2)};
    check(layerCellVisible(materials, 0, 0, 1, -1), "material inside");
    check(layerCellVisible(materials, 0, 0, 0, 2), "liquid material inside");
    check(!layerCellVisible(materials, 0, 0, 0, 3), "both outside");
    check(!layerCellVisible(materials, 0, 0, -1, -1), "no material is never inside");
    // Other modes ignore any stray end bits only through clampLayerValue, which strips them.
    check(layerDisplayModeFromInt(4) == LayerDisplayMode::Range, "mode 4 is the range");
    check(layerDisplayModeFromInt(9) == LayerDisplayMode::Range, "out of range modes clamp");
    std::printf("LayerRangeTests: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
