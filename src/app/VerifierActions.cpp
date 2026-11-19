#include "app/VerifierActions.h"
#include "app/TaskActions.h"
#include "structure/StructureFileLoader.h"
#include "structure/StructureTransform.h"
#include "structure/VerifierComparison.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/ClientInstanceState.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/states/VanillaBlockStateTransformUtils.h"
#include "mc/util/Mirror.h"
#include "mc/util/Rotation.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
namespace lholo::app {
namespace {
VerifierStatus state;
std::atomic<std::shared_ptr<VerifierStatus const>> published{std::make_shared<VerifierStatus>()};
std::mutex mutex;
std::deque<std::pair<std::string,std::string>> commands;
std::shared_ptr<structure::LoadedStructure const> source;
tasks::File placement;
std::uint64_t worldRevision{},cell{};
std::size_t region{};
std::map<std::string,std::size_t> groups;
constexpr std::size_t kMaxGroups=512,kMaxPositions=64; // bounds snapshot copies and the JSON sent to the interface
std::map<Block const*,Block const*> transformed;
std::set<std::string> ignored;
auto lastPublish=std::chrono::steady_clock::now();
void publish(){++state.revision;published.store(std::make_shared<VerifierStatus const>(state));lastPublish=std::chrono::steady_clock::now();}
void clear(){source.reset();transformed.clear();groups.clear();region=0;cell=0;auto revision=state.revision;auto markers=state.showMarkers;state={};state.revision=revision;state.showMarkers=markers;}
LocalPlayer* player(){auto client=ll::service::getClientInstance();return client&&client->$getClientInstanceState()==ClientInstanceState::Playing?client->getLocalPlayer():nullptr;}
Block const* transform(Block const* block){
    if(!block||block->isAir())return nullptr;
    auto found=transformed.find(block);if(found!=transformed.end())return found->second;
    auto* result=VanillaBlockStateTransformUtils::transformBlock(*block,static_cast<Rotation>(placement.rotation/90),placement.mirror=="x"?Mirror::Z:placement.mirror=="z"?Mirror::X:Mirror::None);
    if(!result)throw std::runtime_error("Could not transform schematic block state");
    transformed.emplace(block,result);return result;
}
std::string name(Block const* block){return block&&!block->isAir()?block->getTypeName():"minecraft:air";}
std::string signature(Block const* block){return name(block)+":"+std::to_string(block&&!block->isAir()?block->mSerializationIdHash:0);}
structure::VerificationBlock info(Block const* block,Block const* expected){
    bool air=!block||block->isAir();return {air,air?0:block->mSerializationIdHash,air||name(block)==name(expected)};
}
void start(LocalPlayer& owner){
    auto snapshot=getTaskSnapshot();auto* task=tasks::findTask(snapshot.catalog,snapshot.activeTaskId);
    auto* file=task?tasks::findFile(*task,task->selectedFileId):nullptr;
    if(!file||file->dimension!=static_cast<int>(owner.getDimensionId()))throw std::runtime_error("Select a placement in the current dimension");
    auto loaded=structure::detail::cachedTaskFile(file->path);
    if(!loaded)throw std::runtime_error("Schematic is still loading");
    if(loaded->unmappedBlocks)throw std::runtime_error("Schematic contains unmapped blocks; verification would be incomplete");
    clear();source=std::move(loaded);placement=*file;state.placementId=file->id;state.name=file->name;state.running=true;
    for(auto const& box:source->regions){auto volume=std::uint64_t(box.sizeX)*box.sizeY*box.sizeZ;if(volume>9007199254740991ULL-state.total)throw std::runtime_error("Verification exceeds supported volume");state.total+=volume;}
}
bool overlapsEarlier(std::array<int,3> p){
    for(std::size_t i=0;i<region;++i){auto const& b=source->regions[i];if(p[0]>=b.x&&p[0]-b.x<b.sizeX&&p[1]>=b.y&&p[1]-b.y<b.sizeY&&p[2]>=b.z&&p[2]-b.z<b.sizeZ)return true;}return false;
}
void scan(LocalPlayer& owner){
    auto& world=owner.getDimensionBlockSource();if(world.mOwnerThreadID.get()!=std::this_thread::get_id())throw std::runtime_error("Verification requires the world owner thread");
    auto snapshot=getTaskSnapshot();auto* task=tasks::findTask(snapshot.catalog,snapshot.activeTaskId);auto* file=task?tasks::findFile(*task,placement.id):nullptr;
    if(!file||*file!=placement||file->dimension!=static_cast<int>(owner.getDimensionId())||source!=structure::detail::cachedTaskFile(placement.path))throw std::runtime_error("Placement changed; restart verification");
    state.waitingChunks=false;auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(2);
    for(unsigned budget=0;budget<2048&&std::chrono::steady_clock::now()<deadline;++budget){
        if(region>=source->regions.size()){state.running=false;state.complete=true;state.total=state.checked;publish();return;}
        auto const& box=source->regions[region];auto volume=std::uint64_t(box.sizeX)*box.sizeY*box.sizeZ;
        if(cell>=volume){cell=0;++region;continue;}
        std::array<int,3> p{box.x+static_cast<int>(cell/box.sizeZ/box.sizeY),box.y+static_cast<int>((cell/box.sizeZ)%box.sizeY),box.z+static_cast<int>(cell%box.sizeZ)};
        if(source->isOmittedCell(p[0],p[1],p[2])||overlapsEarlier(p)){++cell;--state.total;continue;}
        auto t=structure::transformCell(p[0],p[1],p[2],source->sizeX,source->sizeZ,placement.mirror=="x"?1:placement.mirror=="z"?2:0,placement.rotation/90);
        std::array<int,3> anchor{placement.x,placement.y,placement.z};
        for(int a=0;a<3;++a){auto v=static_cast<long long>(t[a])+anchor[a];if(v<INT32_MIN||v>INT32_MAX)throw std::runtime_error("Placement exceeds world coordinates");t[a]=static_cast<int>(v);}
        BlockPos position{t[0],t[1],t[2]};
        if(t[1]<world.getMinHeight()||t[1]>=world.getMaxHeight())throw std::runtime_error("Placement exceeds world height");
        if(!world.areChunksFullyLoaded(position,position)){state.waitingChunks=true;break;}
        Block const *expected=nullptr,*liquid=nullptr;
        if(auto index=source->renderBlocks.find(p[0],p[1],p[2])){auto block=source->renderBlocks[*index];expected=transform(block.block);liquid=transform(block.liquid);}
        auto const* actual=&world.getBlock(position);auto const* actualLiquid=&world.getExtraBlock(position);
        auto kind=structure::compareVerificationCell(info(expected,expected),info(actual,expected),info(liquid,liquid),info(actualLiquid,liquid));
        auto category=static_cast<int>(kind);++state.counts[category];++state.checked;++cell;
        if(kind!=structure::VerificationKind::Correct){
            auto id=std::to_string(category)+"|"+signature(expected)+"|"+signature(liquid)+"|"+signature(actual)+"|"+signature(actualLiquid);
            auto found=groups.find(id);
            if(found==groups.end()){
                if(state.groups.size()>=kMaxGroups){++state.unlistedMismatches;continue;}
                found=groups.emplace(id,state.groups.size()).first;
                state.groups.push_back({id,name(expected)+(liquid?" + "+name(liquid):""),name(actual)+(!actualLiquid->isAir()?" + "+name(actualLiquid):""),category,0,ignored.contains(id),{}});
            }
            auto& group=state.groups[found->second];++group.count;
            if(group.positions.size()<kMaxPositions)group.positions.push_back(t);
        }
    }
}
}
VerifierStatus verifierStatus(){return *published.load();}
std::shared_ptr<VerifierStatus const> verifierStatusShared(){return published.load();}
bool queueVerifierCommand(std::string const& command,std::string const& value){std::lock_guard lock(mutex);if(commands.size()>=16)return false;commands.emplace_back(command,value);return true;}
void resetVerifierActions(){std::lock_guard lock(mutex);commands.clear();ignored.clear();clear();publish();}
void processVerifierActions(){
    auto revision=structure::detail::structureFileWorldRevision();if(worldRevision!=revision){worldRevision=revision;ignored.clear();clear();publish();}
    std::deque<std::pair<std::string,std::string>> pending;{std::lock_guard lock(mutex);pending.swap(commands);}
    for(auto const& [command,value]:pending){try{
        if(command=="start"){auto* owner=player();if(!owner)throw std::runtime_error("No active world");start(*owner);}
        else if(command=="stop")state.running=false;
        else if(command=="markers")state.showMarkers=!state.showMarkers;
        else if(command=="reset"){ignored.clear();clear();}
        else if(command=="ignore"){auto found=groups.find(value);if(found==groups.end())throw std::runtime_error("Unknown verifier group");auto& group=state.groups[found->second];group.ignored=!group.ignored;if(group.ignored)ignored.insert(value);else ignored.erase(value);}
        else if(command=="resetIgnored"){ignored.clear();for(auto& group:state.groups)group.ignored=false;}
        else throw std::runtime_error("Unknown verifier command");
    }catch(std::exception const& error){state.error=error.what();state.running=false;}publish();}
    if(state.running){try{auto* owner=player();if(!owner)throw std::runtime_error("World is unavailable");scan(*owner);}catch(std::exception const& error){state.error=error.what();state.running=false;publish();}
        if(std::chrono::steady_clock::now()-lastPublish>=std::chrono::milliseconds(150))publish();}
}
}
