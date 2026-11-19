#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "mc/world/level/BlockPos.h"
#include "structure/capture/StructureCapture.h"

class BaseActorRenderContext;
namespace mce { class Mesh; }

namespace lholo::overlay {

// Area selection drawn like Litematica's OverlayRenderer: box edges coloured by axis (white when the box is not the
// selected sub-region), corner 1 outlined red, corner 2 blue, the selected corner cyan, both on one block magenta,
// and the manual origin orange.
class SelectionOverlay {
public:
    SelectionOverlay() = default;
    ~SelectionOverlay();
    SelectionOverlay(SelectionOverlay const&) = delete;
    SelectionOverlay& operator=(SelectionOverlay const&) = delete;

    void set(structure::capture::SelectionRenderData const& data);
    void clear();
    void render(BaseActorRenderContext& renderContext, bool renderAlphaLayer);

private:
    struct Item {
        BlockPos origin{};
        std::vector<float> segments; // pairs of xyz points relative to origin
        std::uint32_t color{};       // ABGR
        std::unique_ptr<mce::Mesh> mesh;
    };
    structure::capture::SelectionRenderData mData;
    bool mHasData{};
    bool mDirty{true};
    std::vector<Item> mItems;
    void rebuildItems();
};

} // namespace lholo::overlay
