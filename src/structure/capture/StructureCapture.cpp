#include "structure/capture/StructureCapture.h"

#include "i18n/Message.h"
#include "structure/capture/McstructureExporter.h"
#include "structure/capture/BedrockStructureCapture.h"
#include "structure/capture/CaptureBounds.h"
#include "structure/capture/SelectionJson.h"
#include "structure/capture/LitematicWorldExport.h"
#include <fstream>
#include <Windows.h>
#include "plugin/LHolo.h"
#include "ll/api/mod/NativeMod.h"

#include <algorithm>
#include <mutex>
#include <chrono>
#include <exception>
#include <thread>

#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/levelgen/structure/StructureTemplate.h"

namespace lholo::structure::capture {
namespace {

std::mutex    gMutex;
Draft         gDraft;
i18n::Message gStatus{i18n::TextKey::CaptureStatusNeedTwoPoints};
Level*        gLevel{};
Dimension*    gDimension{};
std::uint64_t gRevision{};
std::vector<Snapshot::Saved> gSelections;
std::string gSelectionError, gAnalysis;
std::optional<std::filesystem::path> gExportedPath;
bool selectionsScanned{};
auto selectionDirectory() {return LHolo::getInstance().getSelf().getDataDir()/"area_selections";}
bool validSelectionId(std::string const& id) {
    return !id.empty()&&id.size()<=128&&std::all_of(id.begin(),id.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-';});
}
void scanSelections() {
    gSelections.clear();selectionsScanned=true;
    auto directory=selectionDirectory();
    std::filesystem::create_directories(directory);
    for (auto const& entry:std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()||entry.path().extension()!=L".json") continue;
        auto id=entry.path().stem().string();
        if (!validSelectionId(id)) continue;
        try {std::ifstream in(entry.path());auto json=SelectionJson::parse(in);gSelections.push_back({id,json.value("name",id)});} catch (...) {}
    }
    std::sort(gSelections.begin(),gSelections.end(),[](auto const& a,auto const& b){return a.name<b.name;});
}

struct ClientContext {
    LocalPlayer* player{};
    Level*       level{};
    Dimension*   dimension{};
};

ClientContext currentContext() {
    auto client = ll::service::getClientInstance();
    auto* player = client ? client->getLocalPlayer() : nullptr;
    if (!player) return {};
    return {player, &player->getLevel(), &player->getDimension()};
}

void resetLocked() {
    gDraft = {};
    gStatus = i18n::Message{i18n::TextKey::CaptureStatusNeedTwoPoints};
    ++gRevision;
}

void syncContextLocked(ClientContext const& context) {
    if (!context.player) {
        if (gLevel || gDimension || gDraft.first || gDraft.second) resetLocked();
        gLevel = nullptr;
        gDimension = nullptr;
        return;
    }
    if (gLevel && (gLevel != context.level || gDimension != context.dimension)) resetLocked();
    gLevel = context.level;
    gDimension = context.dimension;
}

Bounds normalizedBounds(Draft const& draft) {
    auto const& first = *draft.first;
    auto const& second = *draft.second;
    return {
        {std::min(first.x, second.x), std::min(first.y, second.y), std::min(first.z, second.z)},
        {std::max(first.x, second.x), std::max(first.y, second.y), std::max(first.z, second.z)}
    };
}

void setStatus(i18n::Message const& status) {
    std::lock_guard lock(gMutex);
    gStatus = status;
}

} // namespace

Snapshot getSnapshot() {
    auto const context = currentContext();
    std::lock_guard lock(gMutex);
    syncContextLocked(context);
    if (!selectionsScanned) try {scanSelections();} catch (std::exception const& error) {gSelectionError=error.what();}
    return {gDraft, static_cast<bool>(context.player), i18n::format(gStatus), gRevision, gSelections, gSelectionError, gAnalysis};
}

std::optional<Bounds> getBounds() {
    auto const context = currentContext();
    std::lock_guard lock(gMutex);
    syncContextLocked(context);
    if (!context.player) return {};
    return enclosingBounds(gDraft);
}
std::vector<Bounds> getRegionBounds() {
    auto context=currentContext();std::lock_guard lock(gMutex);syncContextLocked(context);
    return context.player?selectionBounds(gDraft):std::vector<Bounds>{};
}

SelectionRenderData getSelectionRenderData() {
    auto context=currentContext();std::lock_guard lock(gMutex);syncContextLocked(context);
    SelectionRenderData data;
    if(!context.player)return data;
    if(gDraft.normalMode){
        for(auto const& region:gDraft.regions){
            bool selected=region.id==gDraft.selectedRegion;
            // The selected region's live corners are the draft's own.
            data.boxes.push_back({selected?gDraft.first:region.first,selected?gDraft.second:region.second,selected,selected?gDraft.selectedCorner:0});
        }
    }else if(gDraft.first||gDraft.second){
        data.boxes.push_back({gDraft.first,gDraft.second,true,gDraft.selectedCorner});
    }
    data.origin=gDraft.origin;
    return data;
}

void updateDraft(Draft const& draft) {
    auto const context = currentContext();
    std::lock_guard lock(gMutex);
    syncContextLocked(context);
    if (!context.player) return;
    if (gDraft == draft) return;
    gDraft = draft;
    syncSelectedRegion(gDraft);
    if (gDraft.first && gDraft.second) {
        gStatus = i18n::Message{i18n::TextKey::CaptureStatusSelectionReady};
    } else if (gDraft.first || gDraft.second) {
        gStatus = i18n::Message{i18n::TextKey::CaptureStatusNeedOtherPoint};
    } else {
        gStatus = i18n::Message{i18n::TextKey::CaptureStatusNeedTwoPoints};
    }
    ++gRevision;
}

void setPointFromPlayer(PointSlot slot) {
    auto const context = currentContext();
    if (!context.player) {
        setStatus(i18n::Message{i18n::TextKey::CaptureStatusNoWorld});
        return;
    }
    auto const position = context.player->getFeetBlockPos();

    std::lock_guard lock(gMutex);
    syncContextLocked(context);
    auto& point = slot == PointSlot::Origin ? gDraft.origin : slot == PointSlot::First ? gDraft.first : gDraft.second;
    point = Point{position.x, position.y, position.z};
    syncSelectedRegion(gDraft);
    gStatus = i18n::Message{
        slot == PointSlot::First
            ? i18n::TextKey::CaptureStatusPoint1Recorded
            : i18n::TextKey::CaptureStatusPoint2Recorded
    };
    ++gRevision;
}

void exportStructure(Draft const& draft, std::filesystem::path const& output) {
    auto const context = currentContext();
    if (!context.player) {
        setStatus(i18n::Message{i18n::TextKey::CaptureStatusNoWorld});
        return;
    }
    if (draft.mode != CaptureMode::Client) {
        setStatus(i18n::Message{i18n::TextKey::CaptureStatusSingleplayerUnsupported});
        return;
    }
    if (output.extension()==L".litematic") {
        std::string error;
        bool const exported=exportLitematicWorld(context.player->getDimensionBlockSource(),draft,output,error);
        {std::lock_guard lock(gMutex);gSelectionError=std::move(error);}
        if(exported){std::lock_guard lock(gMutex);gExportedPath=output;}
        setStatus(i18n::Message{exported?i18n::TextKey::CaptureStatusExported:i18n::TextKey::CaptureStatusWriteFailed});
        return;
    }
    if (draft.normalMode && draft.regions.size()>1) {
        std::lock_guard lock(gMutex);gSelectionError="Use .litematic to preserve multiple selection subregions";return;
    }
    if (!draft.first || !draft.second) {
        setStatus(i18n::Message{i18n::TextKey::CaptureStatusNeedBothPoints});
        return;
    }

    Bounds bounds;
    {
        std::lock_guard lock(gMutex);
        syncContextLocked(context);
        if (gDraft != draft) {
            gDraft = draft;
            ++gRevision;
        }
        bounds = normalizedBounds(gDraft);
    }

    BlockPos const min{bounds.min.x, bounds.min.y, bounds.min.z};
    BlockPos const max{bounds.max.x, bounds.max.y, bounds.max.z};
    auto& region = context.player->getDimensionBlockSource();
    // Even read-only block queries mutate BlockSource's last-chunk cache.
    // Enforce its ownership before the first chunk query, including validation.
    if (region.mOwnerThreadID.get() != std::this_thread::get_id()) {
        LHolo::getInstance().getSelf().getLogger().error("[capture] world capture requires the BlockSource owner thread");
        setStatus(i18n::Message{i18n::TextKey::CaptureStatusTemplateFailed});
        return;
    }
    auto const size = captureSize(bounds);
    if (!size) {
        setStatus(i18n::Message{i18n::TextKey::CaptureStatusSizeLimit});
        return;
    }
    if (min.y < region.getMinHeight() || max.y >= region.getMaxHeight()) {
        setStatus(i18n::Message{i18n::TextKey::CaptureStatusHeightLimit});
        return;
    }
    if (!region.areChunksFullyLoaded(min, max)) {
        setStatus(i18n::Message{i18n::TextKey::CaptureStatusRegionNotLoaded});
        return;
    }

    auto& logger = LHolo::getInstance().getSelf().getLogger();
    auto const start = std::chrono::steady_clock::now();
    auto elapsed = [&] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    };
    bool captured{};
    logger.info("[capture] begin min=({}, {}, {}) size=({}, {}, {}) blocks={} entities={}",
        min.x, min.y, min.z, (*size)[0], (*size)[1], (*size)[2],
        static_cast<std::uint64_t>((*size)[0]) * (*size)[1] * (*size)[2], draft.includeEntities);
    try {
        auto structure = captureBedrockStructure(region, min, BlockPos{(*size)[0], (*size)[1], (*size)[2]}, draft.includeEntities);
        captured = true;
        logger.info("[capture] captured in {} ms; writing file", elapsed());
        if (!exportMcstructure(*structure, output)) {
            logger.error("[capture] file export failed after {} ms", elapsed());
            setStatus(i18n::Message{i18n::TextKey::CaptureStatusWriteFailed});
            return;
        }
        logger.info("[capture] exported in {} ms total", elapsed());
    } catch (std::exception const& error) {
        // Ordinary C++ failures become UI errors. Heap corruption/SEH is not
        // swallowed: continuing after a damaged heap would be unsafe.
        logger.error("[capture] {} failed after {} ms: {}", captured ? "file export" : "world capture", elapsed(), error.what());
        setStatus(i18n::Message{captured ? i18n::TextKey::CaptureStatusWriteFailed : i18n::TextKey::CaptureStatusTemplateFailed});
        return;
    }
    {std::lock_guard lock(gMutex);gExportedPath=output;}
    setStatus(i18n::Message{i18n::TextKey::CaptureStatusExported});
}

std::optional<std::filesystem::path> takeExportedPath() {
    std::lock_guard lock(gMutex);
    return std::exchange(gExportedPath,std::nullopt);
}

void selectionCommand(std::string const& action, std::string const& value) {
    auto context=currentContext();
    std::lock_guard lock(gMutex);
    syncContextLocked(context);
    gSelectionError.clear();
    try {
        if (action=="mode") setSelectionMode(gDraft,!gDraft.normalMode);
        else if (action=="cornerMode") gDraft.expandCorners=!gDraft.expandCorners;
        else if (action=="selectCorner") {
            if (value!="0" && value!="1" && value!="2" && value!="3")
                throw std::invalid_argument("Invalid selection corner");
            if (value=="3" && !gDraft.origin)
                throw std::invalid_argument("Manual origin is not enabled");
            gDraft.selectedCorner=value[0]-'0';
        }
        else if (action=="origin") {
            if (gDraft.origin) {
                gDraft.origin.reset();
                if (gDraft.selectedCorner==3) gDraft.selectedCorner=0;
            } else gDraft.origin=selectionOrigin(gDraft);
        } else if (action=="addRegion") {setSelectionMode(gDraft,true);addRegion(gDraft,value);}
        else if (action=="selectRegion") selectRegion(gDraft,value);
        else if (action=="removeRegion") {
            auto id=value.empty()?gDraft.selectedRegion:value;
            std::erase_if(gDraft.regions,[&](auto const& box){return box.id==id;});
            if (gDraft.selectedRegion==id) {
                gDraft.selectedRegion.clear();
                if (!gDraft.regions.empty()) selectRegion(gDraft,gDraft.regions.front().id);
                else {gDraft.first.reset();gDraft.second.reset();}
            }
        } else if (action=="toggleRegion") {
            for (auto& box:gDraft.regions) if (box.id==value) box.enabled=!box.enabled;
        } else if (action=="newSelection") {gDraft={};gDraft.name=value.empty()?"Unnamed":value;setSelectionMode(gDraft,true);}
        else if (action=="saveSelection") {
            syncSelectedRegion(gDraft);
            if (!validSelectionId(gDraft.selectionId)) {
                for (unsigned n=1;;++n) {auto id="selection-"+std::to_string(n);if (!std::filesystem::exists(selectionDirectory()/(id+".json"))) {gDraft.selectionId=id;break;}}
            }
            auto directory=selectionDirectory();std::filesystem::create_directories(directory);
            auto path=directory/(gDraft.selectionId+".json"), temporary=path;temporary+=L".tmp";
            {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out.exceptions(std::ios::badbit|std::ios::failbit);out<<selectionJson(gDraft).dump(2);out.close();}
            if (!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Could not save area selection");
            scanSelections();
        } else if (action=="loadSelection"||action=="deleteSelection") {
            if (!validSelectionId(value)) throw std::runtime_error("Invalid area selection ID");
            auto path=selectionDirectory()/(value+".json");
            if (action=="loadSelection") {std::ifstream in(path);auto json=SelectionJson::parse(in);gDraft=selectionDraft(json);gDraft.selectionId=value;}
            else {auto removed=path;removed+=L".deleted";if (std::filesystem::exists(removed)) removed+=std::to_wstring(GetTickCount64());std::filesystem::rename(path,removed);scanSelections();}
        } else if (action=="analyze") {
            auto boxes=selectionBounds(gDraft);std::uint64_t volume{};
            for (auto const& box:boxes) {auto size=captureSize(box);if(!size)throw std::runtime_error("Selection dimensions exceed supported range");volume+=std::uint64_t((*size)[0])*(*size)[1]*(*size)[2];}
            // Count what is actually in the world inside the enabled regions: block types, air and a bounded top list.
            constexpr std::uint64_t kMaximumAnalysisCells=8'000'000;
            if (volume>kMaximumAnalysisCells) throw std::runtime_error("Selection is too large to analyse ("+std::to_string(volume)+" cells; limit "+std::to_string(kMaximumAnalysisCells)+")");
            if (!context.player) throw std::runtime_error("Analysis needs an active world");
            auto& world=context.player->getDimensionBlockSource();
            if (world.mOwnerThreadID.get()!=std::this_thread::get_id()) throw std::runtime_error("Analysis requires the world owner thread");
            std::map<std::string,std::uint64_t> counts;std::uint64_t air{};
            for (auto const& box:boxes) {
                if (box.min.y<world.getMinHeight()||box.max.y>=world.getMaxHeight()) throw std::runtime_error("Selection exceeds world height");
                if (!world.areChunksFullyLoaded({box.min.x,box.min.y,box.min.z},{box.max.x,box.max.y,box.max.z})) throw std::runtime_error("Selection includes unloaded chunks");
                for (int y=box.min.y;y<=box.max.y;++y) for (int z=box.min.z;z<=box.max.z;++z) for (int x=box.min.x;x<=box.max.x;++x) {
                    BlockPos position{x,y,z};
                    auto const& block=world.getBlock(position);auto const& liquid=world.getExtraBlock(position);
                    if (block.isAir()&&liquid.isAir()) {++air;continue;}
                    if (!block.isAir()) ++counts[block.getTypeName()];
                    if (!liquid.isAir()) ++counts[liquid.getTypeName()];
                }
            }
            std::vector<std::pair<std::string,std::uint64_t>> ordered(counts.begin(),counts.end());
            std::sort(ordered.begin(),ordered.end(),[](auto const& a,auto const& b){return a.second!=b.second?a.second>b.second:a.first<b.first;});
            std::uint64_t solid=volume-air;
            gAnalysis=std::to_string(boxes.size())+" regions, "+std::to_string(volume)+" cells, "+std::to_string(solid)+" non-air, "+std::to_string(air)+" air, "+std::to_string(ordered.size())+" block types";
            for (std::size_t n=0;n<ordered.size()&&n<12;++n) gAnalysis+="\n"+ordered[n].first+": "+std::to_string(ordered[n].second);
            if (ordered.size()>12) gAnalysis+="\n... "+std::to_string(ordered.size()-12)+" more types";
        } else throw std::runtime_error("Unknown selection operation");
    } catch (std::exception const& error) {gSelectionError=error.what();}
    ++gRevision;
}

void clear() {
    std::lock_guard lock(gMutex);
    resetLocked();
}

} // namespace lholo::structure::capture
