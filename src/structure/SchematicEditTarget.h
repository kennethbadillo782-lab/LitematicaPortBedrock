#pragma once

#include "structure/ProjectionRaycast.h"

#include <array>
#include <optional>
#include <string>

namespace lholo::structure {

struct SchematicEditCell {
    std::size_t file{};
    std::string regionName;
    std::array<int, 3> regionCell{};
};

// The compact palette keeps the winning region even where source subregions
// overlap. Inverse placement transforms are applied before subtracting that
// region's minimum corner. No guessed region may receive a destructive edit.
inline std::optional<SchematicEditCell> schematicEditCell(
    TaskProjectionView const& view, ProjectionRayHit const& hit,
    std::array<int, 3> adjacent = {}) {
    if (hit.owner.file >= view.files().size()) return {};
    auto const& file = view.files()[hit.owner.file];
    if (!file.loaded || !file.loaded->sourceArchive ||
        hit.owner.index >= file.loaded->renderBlocks.size()) return {};
    auto const block = file.loaded->renderBlocks[hit.owner.index];
    if (block.regionIndex < 0 ||
        static_cast<std::size_t>(block.regionIndex) >= file.loaded->regions.size()) return {};
    auto const& region = file.loaded->regions[static_cast<std::size_t>(block.regionIndex)];
    if (region.name.empty()) return {};
    auto world = hit.worldCell;
    for (int axis = 0; axis < 3; ++axis) world[axis] += adjacent[axis];
    auto local = view.localCell(hit.owner.file, world);
    if (!local) return {};
    std::array<int, 3> cell{(*local)[0] - region.x,
                            (*local)[1] - region.y,
                            (*local)[2] - region.z};
    if (cell[0] < 0 || cell[0] >= region.sizeX ||
        cell[1] < 0 || cell[1] >= region.sizeY ||
        cell[2] < 0 || cell[2] >= region.sizeZ) return {};
    return SchematicEditCell{hit.owner.file, region.name, cell};
}

} // namespace lholo::structure
