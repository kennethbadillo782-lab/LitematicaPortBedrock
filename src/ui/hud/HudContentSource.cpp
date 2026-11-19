#include "structure/capture/SelectionModel.h"
#include "app/TaskActions.h"
#include "app/ToolActions.h"
#include "app/JavaConfigActions.h"
#include "ui/hud/HudContent.h"
#include "input/MaterialHudPageRequest.h"
#include "ui/hud/editor/HudEditor.h"
#include "structure/StructureLoader.h"
#include "structure/StructureSession.h"
#include "structure/StructureUiState.h"
#include "structure/StructurePaths.h"
#include "projection/Projection.h"
#include "place/PlaceHelper.h"
#include "oreui/OreUiScreen.h"
#include <algorithm>
#include <chrono>
#include "app/StructureWriteActions.h"
#include "structure/write/StructureWriteText.h"

namespace lholo::ui::hud {
namespace {
// Fills the tool HUD the way Litematica's ToolHud does for the current tool mode.
void appendToolHud(HudContent& result, int position) {
    auto const tool = app::toolStatus();
    if (!tool.held&&tool.error.empty()) return;
    using M = app::ToolMode;
    ToolHudData data;
    data.mode = static_cast<int>(tool.mode);
    data.usesSchematic = tool.mode == M::SchematicPlacement || tool.mode == M::PasteSchematic || tool.mode == M::GridPaste || tool.mode == M::Rebuild;
    data.usesAreaSelection = !data.usesSchematic;
    data.usesPrimary = tool.mode == M::Fill || tool.mode == M::ReplaceBlock;
    data.usesSecondary = tool.mode == M::ReplaceBlock;
    data.primary = tool.primary;
    data.secondary = tool.secondary;
    namespace capture = structure::capture;
    auto const draft = capture::getSnapshot().draft;
    data.normalMode = draft.normalMode;
    data.expandCorners = draft.expandCorners;
    data.areaName = draft.name;
    data.hasArea = draft.first || draft.second || !draft.regions.empty() || draft.origin;
    data.originManual = draft.origin.has_value();
    auto const origin = capture::selectionOrigin(draft);
    data.origin = {origin.x, origin.y, origin.z};
    data.boxCount = draft.normalMode ? static_cast<int>(draft.regions.size()) : 1;
    data.subRegionName = draft.boxName;
    if (draft.first) { data.hasP1 = true; data.p1 = {draft.first->x, draft.first->y, draft.first->z}; }
    if (draft.second) { data.hasP2 = true; data.p2 = {draft.second->x, draft.second->y, draft.second->z}; }
    auto snapshot = app::getTaskSnapshot();
    if (auto* task = tasks::findTask(snapshot.catalog, snapshot.activeTaskId)) {
        if (auto* file = tasks::findFile(*task, task->selectedFileId)) {
            data.hasPlacement = true;
            data.placementName = file->name;
            data.origin = {file->x, file->y, file->z};
        }
    }
    auto panel = makeToolHud(data, position);
    if(tool.mode==M::Move&&tool.moveTarget)panel.lines.push_back({std::string(i18n::tr(i18n::TextKey::JavaMoveTarget))+": "+std::to_string((*tool.moveTarget)[0])+", "+std::to_string((*tool.moveTarget)[1])+", "+std::to_string((*tool.moveTarget)[2]),TextTone::Placement});
    if(!tool.error.empty())panel.lines.push_back({tool.error,TextTone::WrongType});
    // Tool lines first, then any write progress already in the panel.
    panel.lines.insert(panel.lines.end(), result.write.lines.begin(), result.write.lines.end());
    result.write = std::move(panel);
}
}

HudContent readHudContent() {
    // Owned exclusively by the HUD render path; input only supplies intent.
    static MaterialHudPaging paging;
    auto const pageDelta = input::consumeMaterialHudPageRequest();
    HudContent result;
    bool const editing = hudLayoutEditorActive();
    if (!editing && !app::javaToolOptions()->rendering) return result;
    if (!editing && oreui::isMenuOnTop()) return result;
    auto& state = structure::detail::StructureUiState::getInstance();
    auto settings = state.hud();
    if (editing) settings.enabled = true;
    auto const materialEnabled = editing || state.materialHudEnabled();
    if (!materialEnabled) paging = {};
    if (editing || state.writeHudEnabled()) {
        auto const write = app::structureWriteStatus();
        auto const command = write.mode == structure::write::WriteMode::ServerCommands;
        auto const phase = command ? structure::write::writeCommandText(write.command)
            : write.busy ? i18n::TextKey::WriteWorldBusy : structure::write::writeResultText(write.result);
        auto const now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        result.write = makeWriteHud({write.operation, write.finishedAt, write.command.done, write.command.total,
            write.busy, command, write.targetName, phase, write.command.failedSkipped}, state.writeHudPosition(), now);
        if (editing && result.write.lines.empty()) result.write = makeHudEditorContent(settings.position, state.materialHudPosition()).write;
    }
    appendToolHud(result, 1); // Litematica's tool HUD sits bottom-left
    if (!settings.enabled && !materialEnabled) return result;
    auto const session = structure::detail::StructureSession::getInstance().snapshot();
    if (!session.hasVisibleSelection() || projection::isDimensionSuspended()) {
        paging = {};
        if (editing) {
            auto example = makeHudEditorContent(settings.position, state.materialHudPosition());
            result.projection = std::move(example.projection); result.materials = std::move(example.materials);
        }
        return result;
    }
    result.context = session.loaded->generation;
    if (settings.enabled) {
        ProjectionHudData data;
        data.fileName = structure::detail::pathToUtf8((session.loaded->composed
            ? structure::detail::pathFromUtf8(session.lastPath) : session.loaded->sourcePath).filename());
        data.axis = session.transform.layerAxis;
        data.mode = session.transform.layerDisplayMode;
        data.layer = session.transform.displayLayer;
        data.maxLayer = data.axis == structure::LayerAxis::Material
            ? std::max(0, static_cast<int>(session.editor->materialCount) - 1)
            : data.axis == structure::LayerAxis::X ? session.maxLayerX : data.axis == structure::LayerAxis::Z ? session.maxLayerZ : session.maxLayerY;
        if (settings.showOverallProgress || settings.showProgress || settings.showWrongState || settings.showWrongType || settings.showExtraBlocks)
            data.progress = projection::getBuildProgress();
        if (settings.showProjectedBlockName) {
            auto const aimed = place::getAimedProjectedBlock();
            data.aimedBlock = aimed.name;
            data.aimedBlockIconId = aimed.iconId;
            data.aimedBlockIconItem = aimed.iconItem;
        }
        using i18n::TextKey;
        if (place::isManualMode()) data.placementMode = i18n::tr(TextKey::ModeManual);
        else if (place::isEnabled()) data.placementMode = i18n::tr(TextKey::ModeEasy);
        else if (place::isRangeEnabled()) data.placementMode = i18n::tr(TextKey::ModeRange);
        result.projection = makeProjectionHud(settings, data);
    }
    if (materialEnabled) {
        auto const materials = state.materialHudSnapshot();
        auto const changed = paging.setContext({session.loaded->generation,
            session.selectedSource, state.materialHudMaxItems(),
            static_cast<int>(session.transform.layerAxis), static_cast<int>(session.transform.layerDisplayMode), session.transform.displayLayer});
        result.materials = editing && !materials.ready
            ? makeHudEditorContent(settings.position, state.materialHudPosition()).materials
            : makeMaterialHud(materials, state.materialHudPosition(), state.materialHudStackConversion(), state.materialHudMaxItems(), &paging, editing || changed ? 0 : pageDelta);
    }
    if (editing && result.projection.lines.empty())
        result.projection = makeHudEditorContent(settings.position, state.materialHudPosition()).projection;
    return result;
}
}
