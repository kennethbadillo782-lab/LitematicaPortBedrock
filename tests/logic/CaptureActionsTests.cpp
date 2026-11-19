#include "oreui/CaptureProtocol.h"
#include "structure/capture/CaptureActions.h"
#include "structure/capture/CaptureBounds.h"
#include "structure/capture/SelectionJson.h"
#include "structure/formats/LitematicExport.h"
#include "structure/formats/EditableLitematic.h"
#include "structure/formats/LitematicInput.h"
#include "nlohmann/json.hpp"
#include <cstdio>
#include <limits>
#include <thread>

// The action runner is tested against a fake world boundary. These tests do
// not read game memory or claim to validate StructureTemplate capture itself.
template<class T> T const* testJavaValue(lholo::structure::detail::JavaNbtTag::Compound const& parent,std::string const& key) {
    auto found=parent.find(key);return found==parent.end()?nullptr:std::get_if<T>(&found->second.value);
}
namespace lholo::structure::capture {
namespace {
Snapshot fake;
int exports{};
std::array<int, 3> exportedSize{};
std::filesystem::path exportedPath;
std::thread::id exportThread;
}
Snapshot getSnapshot() { return fake; }
void updateDraft(Draft const& draft) { if (fake.draft != draft) { fake.draft = draft; ++fake.revision; } }
void setPointFromPlayer(PointSlot slot) {
    (slot == PointSlot::First ? fake.draft.first : fake.draft.second) = Point{1, 64, -2};
    ++fake.revision;
}
void selectionCommand(std::string const& action,std::string const& value) {
    if(action=="mode") setSelectionMode(fake.draft,!fake.draft.normalMode);
    else if(action=="addRegion") addRegion(fake.draft,value);
    ++fake.revision;
}
void clear() { fake.draft = {}; ++fake.revision; }
std::optional<std::filesystem::path> takeExportedPath() { return std::nullopt; }
void exportStructure(Draft const&, std::filesystem::path const& output) {
    ++exports; exportedPath = output; exportThread = std::this_thread::get_id();
}
bool exportEmptyMcstructure(std::array<int, 3> const& size, std::filesystem::path const& output) {
    exportedSize = size;
    exportedPath = output;
    return true;
}
}

int runCaptureActionsTests() {
    using namespace lholo::structure::capture;
    using lholo::oreui::parseCaptureIntent;
    int failures{};
    auto check = [&](bool value, char const* name) {
        if (!value) { ++failures; std::fprintf(stderr, "CaptureActionsTests: %s\n", name); }
    };
    auto const reproduced = captureSize({{0, -60, 0}, {35, -44, 153}});
    check(reproduced == std::optional<std::array<int, 3>>{{36, 17, 154}}, "reported 94248-cell selection is accepted without arbitrary size cap");
    check(captureSize({{-20, -64, -30}, {-20, -64, -30}}) == std::optional<std::array<int, 3>>{{1, 1, 1}},
          "inclusive single cell at negative coordinates");
    check(!captureSize({{1,0,0}, {0,1,1}}), "reversed unnormalized extent is rejected");
    check(!captureSize({{std::numeric_limits<int>::min(), 0, 0}, {std::numeric_limits<int>::max(), 0, 0}}),
          "individually valid int32 coordinates cannot overflow native side-length arithmetic");
    check(!captureSize({{0,0,0}, {49999,49999,0}}), "format-1 dense index length overflow rejected");
    check(!captureSize({{0,0,0}, {std::numeric_limits<int>::max()-1, std::numeric_limits<int>::max()-1, std::numeric_limits<int>::max()-1}}),
          "volume validated before multiplying three large axes");
    nlohmann::json payload{
        {"id", 1}, {"revision", 0}, {"action", "apply"},
        {"draft", {{"first", {{"x", -12}, {"y", 63}, {"z", 8}}},
            {"second", {{"x", 2}, {"y", 64}, {"z", 10}}}, {"includeEntities", true}}}
    };
    auto request = parseCaptureIntent(payload.dump());
    check(request && request->draft.first->x == -12 && request->draft.includeEntities, "valid typed draft");
    auto invalid = payload;
    invalid["draft"]["first"]["x"] = 1.5;
    check(!parseCaptureIntent(invalid.dump()), "fraction rejected");
    invalid["draft"]["first"]["x"] = 4294967295ULL;
    check(!parseCaptureIntent(invalid.dump()), "positive int32 overflow rejected");
    invalid["draft"]["first"]["x"] = 18446744073709551615ULL;
    check(!parseCaptureIntent(invalid.dump()), "uint64 overflow cannot wrap into signed coordinate");
    invalid["draft"]["first"]["x"] = -2147483649LL;
    check(!parseCaptureIntent(invalid.dump()), "negative int32 overflow rejected");
    invalid = payload; invalid["draft"]["first"]["x"] = "12";
    check(!parseCaptureIntent(invalid.dump()), "coordinate string rejected");
    invalid = payload; invalid["draft"]["first"] = nullptr; invalid["action"] = "export";
    check(!parseCaptureIntent(invalid.dump()), "incomplete export rejected");
    invalid = payload; invalid["action"] = "other";
    check(!parseCaptureIntent(invalid.dump()), "unknown action rejected");
    invalid = payload; invalid["id"] = 9007199254740992ULL;
    check(!parseCaptureIntent(invalid.dump()), "unsafe JS request id rejected");
    check(!parseCaptureIntent("{") && !parseCaptureIntent(std::string(4097, ' ')), "malformed and oversized payloads rejected");
    resetActions();
    fake = {}; fake.worldAvailable = true;
    check(submitAction(*request), "apply queued");
    check(!submitAction(*request), "concurrent action rejected");
    processActions();
    check(fake.draft == request->draft && completedAction() == 1 && !actionPending(), "apply commits and acknowledges");
    request->action = Action::FirstFromPlayer; request->revision = fake.revision; request->id = 2;
    submitAction(*request); processActions();
    check(fake.draft.first == Point{1, 64, -2}, "player point uses native boundary");
    request->action = Action::Export; request->revision = fake.revision; request->id = 3;
    request->output = std::filesystem::path{L"D:\\导出\\结构.mcstructure"};
    submitAction(*request); processActions();
    check(exports == 1 && exportedPath == request->output, "export preserves selected Unicode output path");
    request->revision = fake.revision; request->id = 4;
    submitAction(*request); ++fake.revision; processActions();
    check(exports == 1 && completedAction() == 4, "selection/context revision change blocks stale export");
    request->revision = fake.revision; request->id = 5;
    submitAction(*request); fake.worldAvailable = false; processActions();
    check(exports == 1 && completedAction() == 5, "world exit blocks export");
    fake.worldAvailable = true;
    request->action = Action::Clear; request->revision = fake.revision; request->id = 6;
    submitAction(*request); processActions();
    check(!fake.draft.first && !fake.draft.second, "clear removes selection");
    request->action = Action::Export; request->revision = fake.revision; request->id = 7;
    submitAction(*request); resetActions(); processActions();
    check(exports == 1 && !actionPending(), "reset drops queued world action");
    request->revision = fake.revision; request->id = 8;
    bool submitted{};
    std::thread producer([&] { submitted = submitAction(*request); });
    producer.join();
    check(submitted && actionPending() && exports == 1, "foreign-thread producer only queues export data");
    auto const logicThread = std::this_thread::get_id();
    processActions();
    processActions();
    check(exports == 2 && exportThread == logicThread && completedAction() == 8 && !actionPending(),
          "queued export runs once on the consumer thread, never on the producer thread");
    nlohmann::json emptyPayload{{"id", 9}, {"revision", 0}, {"action", "exportEmpty"},
        {"size", {{"x", 32}, {"y", 16}, {"z", 24}}}};
    auto empty = parseCaptureIntent(emptyPayload.dump());
    check(empty && empty->size == std::array<int, 3>{32, 16, 24}, "empty export reads dimensions without world draft");
    emptyPayload["size"]["x"] = 0;
    check(!parseCaptureIntent(emptyPayload.dump()), "zero extent rejected");
    emptyPayload["size"] = {{"x", 512}, {"y", 512}, {"z", 639}};
    check(parseCaptureIntent(emptyPayload.dump()).has_value(), "empty export accepts sizes above the former 512 MiB limit");
    emptyPayload["size"]["z"] = 640;
    check(!parseCaptureIntent(emptyPayload.dump()), "empty structure exceeding file size rejected");
    if (empty) {
        fake.worldAvailable = false;
        auto const draftBefore = fake.draft;
        empty->output = request->output;
        submitAction(*empty);
        resetWorldActions();
        processActions();
        check(completedAction() == 9 && emptyExportSucceeded() && exportedSize == empty->size
              && exportedPath == empty->output && fake.draft == draftBefore,
              "empty export is independent of world state and preserves world draft");
    }
    Draft selection;
    selection.first=Point{-2,4,8};selection.second=Point{6,12,20};
    setSelectionMode(selection,true);
    check(selection.regions.size()==1&&selection.first==Point{-2,4,8},"Normal mode creates a region from the existing simple selection");
    addRegion(selection,"Roof");selection.first=Point{30,40,50};selection.second=Point{35,45,55};syncSelectedRegion(selection);
    auto unionBounds=enclosingBounds(selection);
    check(unionBounds&&unionBounds->min==Point{-2,4,8}&&unionBounds->max==Point{35,45,55},"Enclosing bounds include all enabled regions");
    auto secondId=selection.selectedRegion;
    selectRegion(selection,"region-1");
    check(selection.first==Point{-2,4,8}&&selection.boxName=="Unnamed","Region selection restores its own coordinates and name");
    selection.origin=Point{99,100,101};
    check(selectionOrigin(selection)==Point{99,100,101},"Explicit origin is independent of region bounds");
    auto saved=selectionDraft(selectionJson(selection));
    check(saved==selection,"Selection persistence round trips all subregions and manual origin");
    setSelectionMode(selection,false);
    check(selection.first==Point{-2,4,8}&&selection.second==Point{6,12,20},"Switching back to Simple restores its original corners");
    setSelectionMode(selection,true);selectRegion(selection,secondId);
    check(selection.first==Point{30,40,50},"Normal subregions survive a Simple mode round trip");
    lholo::structure::ExportRegion encoded;
    encoded.name="Roof";encoded.position={-13,7,29};encoded.size={35,2,1};
    encoded.palette={{"minecraft:air",{}},{"minecraft:oak_stairs",{{"facing","east"},{"half","top"}}},{"minecraft:stone",{}}};
    for(int n=0;n<70;++n)encoded.indices.push_back(n%3);
    auto archive=lholo::structure::encodeLitematic({encoded},"House","Builder","Round trip",4325);
    std::string inflateError;auto inflated=lholo::structure::detail::inflateGzip(archive,inflateError);
    check(inflated.has_value(),"Java schematic writer produces valid gzip");
    if(inflated){
        auto root=lholo::structure::detail::JavaNbtReader{*inflated}.readRoot();
        auto metadata=testJavaValue<lholo::structure::detail::JavaNbtTag::Compound>(root,"Metadata");
        check(metadata&&*testJavaValue<std::string>(*metadata,"Name")=="House","Metadata survives Java NBT encoding");
        auto regionTags=testJavaValue<lholo::structure::detail::JavaNbtTag::Compound>(root,"Regions");
        auto region=testJavaValue<lholo::structure::detail::JavaNbtTag::Compound>(*regionTags,"Roof");
        auto position=testJavaValue<lholo::structure::detail::JavaNbtTag::Compound>(*region,"Position");
        check(*testJavaValue<std::int32_t>(*position,"x")==-13,"Manual origin offsets retain signed coordinates");
        auto states=testJavaValue<lholo::structure::detail::JavaNbtTag::LongArray>(*region,"BlockStates");
        bool correct=true;
        for(std::size_t n=0;n<encoded.indices.size();++n){auto bit=n*2;auto value=(*states)[bit/64]>>(bit%64);if((value&3)!=encoded.indices[n])correct=false;}
        check(correct,"Packed Java block states survive long boundaries");
        lholo::structure::detail::JavaNbtWriter copy;copy.named("",lholo::structure::detail::JavaNbtTag{root});
        auto copyBytes=copy.take();auto copied=lholo::structure::detail::JavaNbtReader{copyBytes}.readRoot();
        check(copied.contains("Regions"),"Generic NBT writer can preserve imported region metadata");
        lholo::structure::EditableLitematic edited{archive};
        check(edited.setBlock("Roof", 0, 0, 0, {"minecraft:stone", {}}) && edited.dirty(),
              "editable source changes a region block");
        auto changedArchive = edited.save();
        std::string changedError;
        auto changedBytes = lholo::structure::detail::inflateGzip(changedArchive, changedError);
        auto changedRoot = lholo::structure::detail::JavaNbtReader{*changedBytes, true}.readRoot();
        auto changedMetadata = testJavaValue<lholo::structure::detail::JavaNbtTag::Compound>(changedRoot,"Metadata");
        auto changedRegions = testJavaValue<lholo::structure::detail::JavaNbtTag::Compound>(changedRoot,"Regions");
        auto changedRegion = testJavaValue<lholo::structure::detail::JavaNbtTag::Compound>(*changedRegions,"Roof");
        auto changedStates = testJavaValue<lholo::structure::detail::JavaNbtTag::LongArray>(*changedRegion,"BlockStates");
        check(lholo::structure::detail::packedPaletteIndex(*changedStates,0,2)==2 &&
              lholo::structure::detail::packedPaletteIndex(*changedStates,1,2)==1 &&
              *testJavaValue<std::int32_t>(*changedMetadata,"TotalBlocks")==47,
              "saved schematic keeps neighboring blocks and updates block count");
        using Tag = lholo::structure::detail::JavaNbtTag;
        auto completeRoot = lholo::structure::detail::JavaNbtReader{*inflated,true}.readRoot();
        auto& completeRegion = std::get<Tag::Compound>(
            std::get<Tag::Compound>(completeRoot.at("Regions").value).at("Roof").value);
        completeRegion["Entities"] = Tag{Tag::List{Tag{Tag::Compound{{"id",Tag{std::string{"minecraft:pig"}}}}}}};
        completeRegion["TileEntities"] = Tag{Tag::List{Tag{Tag::Compound{
            {"x",Tag{std::int32_t{1}}},{"y",Tag{std::int32_t{0}}},{"z",Tag{std::int32_t{0}}},
            {"id",Tag{std::string{"minecraft:chest"}}}}}}};
        lholo::structure::detail::JavaNbtWriter completeWriter;
        completeWriter.named("",Tag{completeRoot});
        auto completeArchive = lholo::structure::detail::gzipJavaNbt(completeWriter.take());
        lholo::structure::EditableLitematic entityEdit{completeArchive};
        check(entityEdit.setBlock("Roof",1,0,0,{"minecraft:stone",{}}),
              "editing a block with a block entity succeeds");
        auto entityBytes = lholo::structure::detail::inflateGzip(entityEdit.save(),changedError);
        auto entityRoot = lholo::structure::detail::JavaNbtReader{*entityBytes,true}.readRoot();
        auto entityRegion = testJavaValue<Tag::Compound>(
            *testJavaValue<Tag::Compound>(entityRoot,"Regions"),"Roof");
        check(testJavaValue<Tag::List>(*entityRegion,"Entities")->size()==1 &&
              testJavaValue<Tag::List>(*entityRegion,"TileEntities")->empty(),
              "save preserves ordinary entities and removes a stale block entity");
        lholo::structure::ExportRegion expanded;
        expanded.name="PaletteGrowth";expanded.size={33,1,1};
        expanded.palette={{"minecraft:air",{}},{"minecraft:stone",{}},
                          {"minecraft:dirt",{}},{"minecraft:glass",{}}};
        for(int n=0;n<33;++n)expanded.indices.push_back(n%4);
        auto growthArchive=lholo::structure::encodeLitematic({expanded},"Palette","Builder","",4325);
        lholo::structure::EditableLitematic growth{growthArchive};
        check(growth.setBlock("PaletteGrowth",32,0,0,{"minecraft:oak_planks",{}}),
              "fifth state grows a two-bit palette to three bits");
        check(growth.setBlock("PaletteGrowth",21,0,0,{"minecraft:dirt",{}}),
              "editing a packed state spanning two words succeeds without palette growth");
        auto growthBytes=lholo::structure::detail::inflateGzip(growth.save(),changedError);
        auto growthRoot=lholo::structure::detail::JavaNbtReader{*growthBytes,true}.readRoot();
        auto growthRegion=testJavaValue<Tag::Compound>(
            *testJavaValue<Tag::Compound>(growthRoot,"Regions"),"PaletteGrowth");
        auto growthStates=testJavaValue<Tag::LongArray>(*growthRegion,"BlockStates");
        bool growthMatches=true;
        for(int n=0;n<33;++n)
            if(lholo::structure::detail::packedPaletteIndex(*growthStates,n,3)!=
               (n==32?4u:n==21?2u:unsigned(n%4)))
                growthMatches=false;
        check(growthMatches,"palette growth preserves states across packed word boundaries");
    }
    std::printf("CaptureActionsTests: %d failures\n", failures);
    return failures;
}
