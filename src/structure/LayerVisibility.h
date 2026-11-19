#pragma once
#include "structure/LayerSettings.h"
namespace lholo::structure {
inline bool layerCellVisible(LayerSettings const &slice, int x, int y, int material = -1, int liquidMaterial = -1, int z = 0) {
    if (slice.mode == LayerDisplayMode::All)
        return true;
    auto included = [&](int coordinate) {
        switch (slice.mode) {
        case LayerDisplayMode::Single:
            return coordinate == slice.layer;
        case LayerDisplayMode::UpToCurrent:
            return coordinate <= slice.layer;
        case LayerDisplayMode::FromCurrent:
            return coordinate >= slice.layer;
        case LayerDisplayMode::Range:
            return coordinate >= layerRangeStart(slice.layer) && coordinate <= layerRangeEnd(slice.layer);
        default:
            return true;
        }
    };
    if (slice.axis == LayerAxis::Material)
        return (material >= 0 && included(material)) || (liquidMaterial >= 0 && included(liquidMaterial));
    return included(layerCoordinate(slice.axis, x, y, z));
}
} // namespace lholo::structure
