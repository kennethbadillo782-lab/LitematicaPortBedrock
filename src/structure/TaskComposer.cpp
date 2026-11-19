#include "structure/TaskComposer.h"
#include <algorithm>
#include <limits>

namespace lholo::structure {
void normalizeCompositionLayers(std::span<CompositionFile> files) {
    for (auto& file : files) {
        if (!file.loaded) continue;
        auto const& loaded = *file.loaded;
        auto const maximum = file.slice.axis == LayerAxis::Material
            ? static_cast<int>(std::min(loaded.materialCount,
                static_cast<std::size_t>(std::numeric_limits<int>::max()))) - 1
            : layerCoordinate(file.slice.axis, loaded.sizeX, loaded.sizeY, loaded.sizeZ) - 1;
        file.slice.layer = std::clamp(file.slice.layer, 0, std::max(0, maximum));
    }
}

} // namespace lholo::structure
