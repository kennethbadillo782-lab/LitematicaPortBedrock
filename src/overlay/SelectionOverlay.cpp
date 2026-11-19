#include "overlay/SelectionOverlay.h"

#include "render/BoxWireframeMesh.h"
#include "render/OverlayMaterials.h"
#include "render/RenderCamera.h"
#include "render/ScopedShaderColor.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/deps/minecraft_renderer/framebuilder/dragon/RenderMetadata.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/deps/renderer/Camera.h"

#include <algorithm>

namespace lholo::overlay {
namespace {

OffscreenCaptureDescription const& emptyOffscreenCaptureDescription() {
    using Storage = decltype(dragon::RenderMetadata::mOffscreenCaptureDescription);
    static Storage empty{};
    return empty.get();
}

// ABGR colours taken from Litematica's OverlayRenderer.
constexpr std::uint32_t abgr(float r, float g, float b) {
    return 0xFF000000u | (static_cast<std::uint32_t>(b * 255.f + .5f) << 16) | (static_cast<std::uint32_t>(g * 255.f + .5f) << 8)
         | static_cast<std::uint32_t>(r * 255.f + .5f);
}
constexpr auto kPos1 = abgr(1.f, 0.0625f, 0.0625f);
constexpr auto kPos2 = abgr(0.0625f, 0.0625f, 1.f);
constexpr auto kOverlap = abgr(1.f, 0.0625f, 1.f);
constexpr auto kSelectedCorner = abgr(0.f, 1.f, 1.f);
constexpr auto kOrigin = abgr(1.f, 144.f / 255.f, 16.f / 255.f);
constexpr auto kAxisX = abgr(1.f, 0.25f, 0.25f);
constexpr auto kAxisY = abgr(0.25f, 1.f, 0.25f);
constexpr auto kAxisZ = abgr(0.25f, 0.25f, 1.f);
constexpr auto kArea = abgr(1.f, 1.f, 1.f);

void addSegment(std::vector<float>& out, float x0, float y0, float z0, float x1, float y1, float z1) {
    out.insert(out.end(), {x0, y0, z0, x1, y1, z1});
}
// The twelve edges of a box from (0,0,0) to (sx,sy,sz), split by direction.
void boxEdges(std::vector<float>& x, std::vector<float>& y, std::vector<float>& z, float sx, float sy, float sz, float e) {
    for (float py : {-e, sy + e}) for (float pz : {-e, sz + e}) addSegment(x, -e, py, pz, sx + e, py, pz);
    for (float px : {-e, sx + e}) for (float pz : {-e, sz + e}) addSegment(y, px, -e, pz, px, sy + e, pz);
    for (float px : {-e, sx + e}) for (float py : {-e, sy + e}) addSegment(z, px, py, -e, px, py, sz + e);
}
void blockOutline(std::vector<float>& out, float e) {
    std::vector<float> x, y, z;
    boxEdges(x, y, z, 1.f, 1.f, 1.f, e);
    out.insert(out.end(), x.begin(), x.end());
    out.insert(out.end(), y.begin(), y.end());
    out.insert(out.end(), z.begin(), z.end());
}

} // namespace

SelectionOverlay::~SelectionOverlay() = default;

void SelectionOverlay::set(structure::capture::SelectionRenderData const& data) {
    if (mHasData && mData == data) return;
    mData = data;
    mHasData = true;
    mDirty = true;
}

void SelectionOverlay::clear() {
    mData = {};
    mHasData = false;
    mDirty = true;
    mItems.clear();
}

void SelectionOverlay::rebuildItems() {
    mItems.clear();
    constexpr float kExpand = 0.001f;
    for (auto const& box : mData.boxes) {
        auto const& a = box.first;
        auto const& b = box.second;
        if (a && b && !(*a == *b)) {
            int const x0 = std::min(a->x, b->x), y0 = std::min(a->y, b->y), z0 = std::min(a->z, b->z);
            int const x1 = std::max(a->x, b->x) + 1, y1 = std::max(a->y, b->y) + 1, z1 = std::max(a->z, b->z) + 1;
            Item ex, ey, ez;
            ex.origin = ey.origin = ez.origin = {x0, y0, z0};
            ex.color = box.selected ? kAxisX : kArea;
            ey.color = box.selected ? kAxisY : kArea;
            ez.color = box.selected ? kAxisZ : kArea;
            boxEdges(ex.segments, ey.segments, ez.segments, static_cast<float>(x1 - x0), static_cast<float>(y1 - y0),
                static_cast<float>(z1 - z0), 0.f);
            mItems.push_back(std::move(ex));
            mItems.push_back(std::move(ey));
            mItems.push_back(std::move(ez));
        }
        auto corner = [&](structure::capture::Point const& p, std::uint32_t color) {
            Item item;
            item.origin = {p.x, p.y, p.z};
            item.color = color;
            blockOutline(item.segments, kExpand);
            mItems.push_back(std::move(item));
        };
        if (a && b && *a == *b) corner(*a, kOverlap);
        else {
            if (a) corner(*a, box.selected && box.selectedCorner == 1 ? kSelectedCorner : kPos1);
            if (b) corner(*b, box.selected && box.selectedCorner == 2 ? kSelectedCorner : kPos2);
        }
    }
    if (mData.origin) {
        Item item;
        item.origin = {mData.origin->x, mData.origin->y, mData.origin->z};
        item.color = kOrigin;
        blockOutline(item.segments, kExpand);
        mItems.push_back(std::move(item));
    }
}

void SelectionOverlay::render(BaseActorRenderContext& renderContext, bool renderAlphaLayer) {
    if (!mHasData) return;
    if (!renderAlphaLayer) {
        if (mDirty) {
            rebuildItems();
            for (auto& item : mItems) {
                std::vector<Vec3> vertices;
                vertices.reserve(item.segments.size() / 3);
                for (std::size_t i = 0; i + 2 < item.segments.size(); i += 3)
                    vertices.push_back({item.segments[i], item.segments[i + 1], item.segments[i + 2]});
                item.mesh = render::buildLineListMesh(renderContext.mScreenContext.tessellator, vertices, "LHoloAreaSelection");
            }
            mDirty = false;
        }
        return;
    }
    auto& client = renderContext.mClientInstance;
    auto* levelRenderer = client.getLevelRenderer();
    if (!levelRenderer) return;
    auto const& material = render::selectionOutlineMaterial(*levelRenderer);
    if (!render::materialExists(material)) return;
    auto const texture = render::resolveWhiteTextureVariant(*levelRenderer);
    Vec3 const camera = render::cameraPosition(renderContext);
    for (auto& item : mItems) {
        if (!item.mesh || !item.mesh->isValid()) continue;
        auto matrix = renderContext.mScreenContext.camera.worldMatrixStack.get().push(false);
        matrix.mat->translate(static_cast<float>(item.origin.x) - camera.x, static_cast<float>(item.origin.y) - camera.y,
            static_cast<float>(item.origin.z) - camera.z);
        render::ScopedShaderColor color{renderContext.mScreenContext.currentShaderColor, item.color};
        item.mesh->renderMesh(renderContext.mScreenContext, material, texture, 0, item.mesh->mVertexCount.get().value_or(0u),
            emptyOffscreenCaptureDescription(), nullptr);
    }
}

} // namespace lholo::overlay
