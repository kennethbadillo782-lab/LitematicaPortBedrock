#include "mc/util/Rotation.h"
#include "mc/util/Mirror.h"
#include "mc/world/level/block/states/VanillaBlockStateTransformUtils.h"
#include "app/JavaConfigActions.h"
#include "app/ToolActions.h"
#include "app/TaskActions.h"
#include "app/TaskContext.h"
#include "app/StructureWriteActions.h"
#include "structure/write/command/RealmCompanion.h"
#include "structure/capture/SelectionModel.h"
#include "structure/capture/SelectionInteraction.h"
#include "structure/capture/CaptureBounds.h"
#include "structure/StructureFileLoader.h"
#include "structure/StructureSession.h"
#include "structure/StructureUiState.h"
#include "structure/IndependentTaskFiles.h"
#include "structure/ProjectionRaycast.h"
#include "structure/SchematicEditTarget.h"
#include "structure/java_to_bedrock/JavaToBedrock.h"
#include "structure/write/StructureWritePlan.h"
#include "structure/write/StructureWriteIndices.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/ClientInstanceState.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/phys/HitResult.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include <Windows.h>
#include <cmath>
#include <limits>
#include <optional>
#include <atomic>
#include <deque>
#include <map>
#include <sstream>
#include <mutex>

namespace lholo::app {
namespace {
using namespace structure::capture;
using namespace structure::write;
ToolStatus state;
std::atomic<std::shared_ptr<ToolStatus const>> published{std::make_shared<ToolStatus>()};
std::atomic_bool armed{};
std::atomic_int mouse{}, key{};
std::atomic_uint heldButtons{};
std::uint64_t worldRevision{}, operation{};
std::optional<int> toolDimension;
std::optional<Draft> moveSource,moveDestination;
std::deque<std::shared_ptr<StructureWritePlan>> plans;
std::string primaryData,secondaryData;
std::deque<WriteTarget> gridTargets;
std::mutex commandMutex;
std::deque<std::pair<std::string,std::string>> commands;

void publish(){++state.revision;published.store(std::make_shared<ToolStatus const>(state));}
LocalPlayer* player(){auto client=ll::service::getClientInstance();return client&&client->$getClientInstanceState()==ClientInstanceState::Playing?client->getLocalPlayer():nullptr;}
std::array<int,3> position(Point p){return {p.x,p.y,p.z};}
Block const& primaryBlock(){
    if(!primaryData.empty()){auto tag=CompoundTag::fromBinaryNbt(primaryData);auto block=tag?Block::tryGetFromRegistry(*tag):optional_ref<Block const>{};if(block)return *block;}
    auto block=Block::tryGetFromRegistry(HashedString(state.primary));if(!block)throw std::runtime_error("Primary block is unavailable");return *block;
}
Block const& secondaryBlock(){
    if(!secondaryData.empty()){auto tag=CompoundTag::fromBinaryNbt(secondaryData);auto block=tag?Block::tryGetFromRegistry(*tag):optional_ref<Block const>{};if(block)return *block;}
    auto block=Block::tryGetFromRegistry(HashedString(state.secondary));if(!block)throw std::runtime_error("Secondary block is unavailable");return *block;
}
std::shared_ptr<StructureWritePlan> areaPlan(LocalPlayer& owner,Bounds const& box,ToolMode mode){
    auto size=captureSize(box);if(!size)throw std::runtime_error("Invalid selection size");
    auto volume=writeVolume(*size);if(volume>4*1024*1024)throw std::runtime_error("Tool operation exceeds four million cells per region");
    auto& world=owner.getDimensionBlockSource();
    if(box.min.y<world.getMinHeight()||box.max.y>=world.getMaxHeight())throw std::runtime_error("Selection exceeds world height");
    if(!world.areChunksFullyLoaded({box.min.x,box.min.y,box.min.z},{box.max.x,box.max.y,box.max.z}))throw std::runtime_error("Selection includes unloaded chunks");
    auto plan=std::make_shared<StructureWritePlan>();plan->size=*size;
    plan->target={position(box.min),static_cast<int>(owner.getDimensionId()),0,0};
    plan->sourceWorldOrigin=position(box.min);plan->regions.push_back({{0,0,0},*size});
    if(mode==ToolMode::Delete)return plan;
    auto const* primary=mode==ToolMode::Move?nullptr:&primaryBlock();auto const* secondary=mode==ToolMode::ReplaceBlock?&secondaryBlock():nullptr;
    std::map<Block const*,int> palette;
    auto index=[&](Block const& block){if(block.isAir())return -1;auto [found,added]=palette.emplace(&block,static_cast<int>(plan->palette.size()));if(added)plan->palette.push_back(block.mSerializationId->toBinaryNbt());return found->second;};
    plan->skipAir=mode==ToolMode::ReplaceBlock;
    for(int x=0;x<(*size)[0];++x)for(int y=0;y<(*size)[1];++y)for(int z=0;z<(*size)[2];++z){
        BlockPos p{box.min.x+x,box.min.y+y,box.min.z+z};
        if(mode==ToolMode::Move){
            auto const& block=world.getBlock(p);auto const& liquid=world.getExtraBlock(p);
            if(block.isAir()&&liquid.isAir())continue;
            std::string nbt;if(auto* actor=world.getBlockEntity(p)){CompoundTag tag;if(!actor->save(tag,SaveContext{}))throw std::runtime_error("Could not capture block entity");nbt=tag.toBinaryNbt();}
            plan->cells.push_back({{x,y,z},index(block),index(liquid),std::move(nbt)});
        }else{
            if(mode==ToolMode::ReplaceBlock&&world.getBlock(p).mSerializationIdHash!=secondary->mSerializationIdHash)continue;
            if(primary->isAir()){
                // An explicit air palette entry distinguishes replacement with
                // air from an omitted cell in a sparse replacement plan.
                auto [found,added]=palette.emplace(primary,static_cast<int>(plan->palette.size()));if(added)plan->palette.push_back(primary->mSerializationId->toBinaryNbt());
                plan->cells.push_back({{x,y,z},found->second,-1,{}});
            }else plan->cells.push_back({{x,y,z},index(*primary),-1,{}});
        }
    }
    return plan;
}
void prepareOperation(LocalPlayer& owner){
    if(state.running||structureWriteStatus().busy)throw std::runtime_error("Another operation is already running");
    if(javaConfigReady()&&javaToolOptions()->requireHeld&&owner.getSelectedItem().getTypeName()!=javaToolOptions()->item)throw std::runtime_error("Hold the configured Litematica tool to execute this operation");
    if(!owner.isCreative()&&!structureWriteStatus().localAvailable&&!structure::write::command::realmCompanionEnabled())
        throw std::runtime_error("Survival tool writes require an integrated local world; remote writes need server command permission");
    if(state.mode==ToolMode::Move&&!state.moveTarget)throw std::runtime_error("Select a move destination with the tool before executing");
    plans.clear();gridTargets.clear();moveSource.reset();moveDestination.reset();state.done=state.total=0;state.error.clear();
    auto mode=state.mode;
    if(mode==ToolMode::AreaSelection||mode==ToolMode::SchematicPlacement||mode==ToolMode::Rebuild)throw std::runtime_error("Use the tool mouse buttons in this mode");
    if(mode==ToolMode::PasteSchematic||mode==ToolMode::GridPaste){
        auto catalog=getTaskSnapshot();auto* manager=tasks::findTask(catalog.catalog,catalog.activeTaskId);
        auto* file=manager?tasks::findFile(*manager,manager->selectedFileId):nullptr;
        if(!file||file->dimension!=static_cast<int>(owner.getDimensionId()))throw std::runtime_error("Select a placement in the current dimension");
        auto source=structure::detail::cachedTaskFile(file->path);if(!source||source->unmappedBlocks)throw std::runtime_error("The schematic contains unavailable block mappings");
        auto neg=mode==ToolMode::GridPaste?state.gridNegative:std::array<int,3>{};
        auto pos=mode==ToolMode::GridPaste?state.gridPositive:std::array<int,3>{};
        std::size_t copies=1;for(int a=0;a<3;++a)copies*=1+neg[a]+pos[a];if(copies>1024)throw std::runtime_error("Grid exceeds 1024 placements");
        WriteTarget baseTarget{{file->x,file->y,file->z},*file->dimension,file->rotation/90,file->mirror=="x"?1:file->mirror=="z"?2:0};
        auto base=std::make_shared<StructureWritePlan>(makeWritePlan(*source,baseTarget));
        plans.push_back(base);
        for(int x=-neg[0];x<=pos[0];++x)for(int y=-neg[1];y<=pos[1];++y)for(int z=-neg[2];z<=pos[2];++z){
            WriteTarget placement{{file->x,file->y,file->z},*file->dimension,file->rotation/90,file->mirror=="x"?1:file->mirror=="z"?2:0};
            std::array<int,3> repeat{x,y,z},size{source->sizeX,source->sizeY,source->sizeZ};if(placement.rotation&1)std::swap(size[0],size[2]);
            for(int a=0;a<3;++a){auto value=static_cast<long long>(placement.position[a])+static_cast<long long>(repeat[a])*(state.gridStep[a]?state.gridStep[a]:size[a]);if(value<INT32_MIN||value>INT32_MAX)throw std::runtime_error("Grid coordinate exceeds int32");placement.position[a]=static_cast<int>(value);}
            std::array<int,3> maximum;if(!writeBounds(placement,base->size,maximum))throw std::runtime_error("Grid exceeds supported coordinates");
            gridTargets.push_back(placement);
        }
    }else{
        auto draft=structure::capture::getSnapshot().draft;auto boxes=selectionBounds(draft);
        std::uint64_t totalCells=0;for(auto const& box:boxes){auto size=captureSize(box);if(!size)throw std::runtime_error("Invalid selection size");auto cells=writeVolume(*size);if(cells>4*1024*1024-totalCells)throw std::runtime_error("Tool selection exceeds four million cells");totalCells+=cells;}
        if(boxes.empty())throw std::runtime_error("No complete selection regions");
        std::vector<Bounds> destinations;
        auto origin=selectionOrigin(draft);
        for(auto const& box:boxes){
            auto plan=areaPlan(owner,box,mode);
            if(mode==ToolMode::Move){
                std::array<long long,3> delta{static_cast<long long>((*state.moveTarget)[0])-origin.x,static_cast<long long>((*state.moveTarget)[1])-origin.y,static_cast<long long>((*state.moveTarget)[2])-origin.z};
                for(int a=0;a<3;++a){auto value=static_cast<long long>(plan->target.position[a])+delta[a];if(value<INT32_MIN||value>INT32_MAX)throw std::runtime_error("Move coordinate exceeds int32");plan->target.position[a]=static_cast<int>(value);}
                auto const& p=plan->target.position;auto const& sz=plan->size;
                std::array<int,3> maximum;if(!writeBounds(plan->target,sz,maximum))throw std::runtime_error("Move exceeds supported coordinates");
                destinations.push_back({{p[0],p[1],p[2]},{maximum[0],maximum[1],maximum[2]}});
            }
            plans.push_back(std::move(plan));
        }
        if(mode==ToolMode::Move){
            moveSource=draft;moveDestination=selectionMovedTo(draft,{(*state.moveTarget)[0],(*state.moveTarget)[1],(*state.moveTarget)[2]});
            auto air=Block::tryGetFromRegistry(HashedString("minecraft:air"));if(!air)throw std::runtime_error("Air block unavailable");
            for(auto const& box:boxes){
                auto erase=areaPlan(owner,box,ToolMode::Delete);erase->skipAir=true;erase->palette.push_back(air->mSerializationId->toBinaryNbt());
                for(int x=0;x<erase->size[0];++x)for(int y=0;y<erase->size[1];++y)for(int z=0;z<erase->size[2];++z){
                    Point p{box.min.x+x,box.min.y+y,box.min.z+z};
                    bool retained=std::any_of(destinations.begin(),destinations.end(),[&](auto const& destination){return p.x>=destination.min.x&&p.x<=destination.max.x&&p.y>=destination.min.y&&p.y<=destination.max.y&&p.z>=destination.min.z&&p.z<=destination.max.z;});
                    if(!retained)erase->cells.push_back({{x,y,z},0,-1,{}});
                }
                plans.push_back(std::move(erase));
            }
        }
    }
    state.total=gridTargets.empty()?plans.size():gridTargets.size();state.running=!plans.empty();operation=0;
}
// Litematica traces the tool's ray up to 200 blocks (KeyCallbacks, maxDistance = 200), not only to the normal reach.
struct WorldBlockHit { Point point; float distance{}; };
std::optional<WorldBlockHit> targetedWorldBlock(LocalPlayer& owner,int maxDistance){
    Vec3 const eye=owner.getEyePos();Vec3 const raw=owner.getViewVector(1.0f);
    float const length=std::sqrt(raw.x*raw.x+raw.y*raw.y+raw.z*raw.z);if(length<=0.0f)return std::nullopt;
    Vec3 const dir{raw.x/length,raw.y/length,raw.z/length};
    auto& world=owner.getDimensionBlockSource();
    int x=static_cast<int>(std::floor(eye.x)),y=static_cast<int>(std::floor(eye.y)),z=static_cast<int>(std::floor(eye.z));
    int const sx=dir.x>0?1:-1,sy=dir.y>0?1:-1,sz=dir.z>0?1:-1;
    float const inf=std::numeric_limits<float>::infinity();
    float const dx=dir.x!=0?std::abs(1.0f/dir.x):inf,dy=dir.y!=0?std::abs(1.0f/dir.y):inf,dz=dir.z!=0?std::abs(1.0f/dir.z):inf;
    float tx=dir.x==0?inf:(sx>0?(x+1.0f-eye.x):(eye.x-x))*dx;
    float ty=dir.y==0?inf:(sy>0?(y+1.0f-eye.y):(eye.y-y))*dy;
    float tz=dir.z==0?inf:(sz>0?(z+1.0f-eye.z):(eye.z-z))*dz;
    if(!world.areChunksFullyLoaded({x,y,z},{x,y,z}))return std::nullopt;
    if(!world.getBlock({x,y,z}).isAir())return WorldBlockHit{{x,y,z},0};
    for(int step=0;step<maxDistance*4;++step){
        float t;
        if(tx<ty&&tx<tz){x+=sx;t=tx;tx+=dx;}else if(ty<tz){y+=sy;t=ty;ty+=dy;}else{z+=sz;t=tz;tz+=dz;}
        if(t>static_cast<float>(maxDistance))break;
        if(y<world.getMinHeight()||y>=world.getMaxHeight())break;
        if(!world.areChunksFullyLoaded({x,y,z},{x,y,z}))return std::nullopt;
    if(!world.getBlock({x,y,z}).isAir())return WorldBlockHit{{x,y,z},t};
    }
    return std::nullopt;
}
// A placement rotates and mirrors the source schematic when it is drawn. A block placed through the projection must be
// stored un-transformed, so the inverse is applied and then checked: transforming the stored state again must give back
// exactly the requested block. If neither order of rotation and mirror does, the edit is refused instead of guessed.
std::optional<structure::JavaBlockState> sourceStateFor(Block const& block,int rotationQuarterTurns,int mirrorMode){
    auto const mirror=mirrorMode==1?Mirror::Z:mirrorMode==2?Mirror::X:Mirror::None;
    auto const forward=static_cast<Rotation>(((rotationQuarterTurns%4)+4)%4);
    auto const inverse=static_cast<Rotation>((4-((rotationQuarterTurns%4)+4)%4)%4);
    auto apply=[](Block const& b,Rotation r,Mirror m)->Block const*{return VanillaBlockStateTransformUtils::transformBlock(b,r,m);};
    Block const* candidates[2]{};
    if(auto* first=apply(block,inverse,Mirror::None))candidates[0]=apply(*first,Rotation::None,mirror);
    if(auto* first=apply(block,Rotation::None,mirror))candidates[1]=apply(*first,inverse,Mirror::None);
    for(auto* candidate:candidates){
        if(!candidate)continue;
        auto* again=apply(*candidate,forward,mirror);
        if(!again||again->mSerializationIdHash!=block.mSerializationIdHash)continue;
        if(auto state=structure::exportJavaBlockState(*candidate,false))return state;
    }
    return std::nullopt;
}
std::optional<Point> targetedPoint(LocalPlayer& owner,int maxDistance){
    auto hit=targetedWorldBlock(owner,maxDistance);
    return hit?std::optional<Point>{hit->point}:std::nullopt;
}
void click(LocalPlayer& owner,int button,int capturedPick=-1){
    bool const primaryPick=(state.mode==ToolMode::Fill||state.mode==ToolMode::ReplaceBlock||state.mode==ToolMode::Rebuild)&&(capturedPick>=0?capturedPick==1:(javaConfigReady()?javaModifierHeld("toolSelectModifierBlock1"):(GetAsyncKeyState(VK_MENU)&0x8000)!=0));
    bool const secondaryPick=state.mode==ToolMode::ReplaceBlock&&(capturedPick>=0?capturedPick==2:(javaConfigReady()?javaModifierHeld("toolSelectModifierBlock2"):(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0));
    if(button==3&&!primaryPick&&!secondaryPick){
        if(state.mode==ToolMode::AreaSelection||state.mode==ToolMode::Fill||state.mode==ToolMode::ReplaceBlock||state.mode==ToolMode::Move||state.mode==ToolMode::Delete){
            auto draft=structure::capture::getSnapshot().draft;auto eye=owner.getEyePos(),direction=owner.getViewVector(1.0f);
            if(selectElementByRay(draft,{eye.x,eye.y,eye.z},{direction.x,direction.y,direction.z}))structure::capture::updateDraft(draft);
        }else if(state.mode==ToolMode::SchematicPlacement||state.mode==ToolMode::PasteSchematic||state.mode==ToolMode::GridPaste){
            auto snapshot=getTaskSnapshot();auto eye=owner.getEyePos(),direction=owner.getViewVector(1.0f);double nearest=200;
            std::string selectedTask,selectedFile;
            for(auto const& task:snapshot.catalog.tasks)for(auto const& file:task.files){
                if(!file.visible||file.dimension!=static_cast<int>(owner.getDimensionId()))continue;
                auto source=structure::detail::cachedTaskFile(file.path);if(!source)continue;
                std::array<int,3> size{source->sizeX,source->sizeY,source->sizeZ};if((file.rotation/90)&1)std::swap(size[0],size[2]);
                std::array<int,3> maximum;if(!writeBounds({{file.x,file.y,file.z},*file.dimension,0,0},size,maximum))continue;
                auto hit=intersectSelectionBox({eye.x,eye.y,eye.z},{direction.x,direction.y,direction.z},{file.x,file.y,file.z},{maximum[0],maximum[1],maximum[2]},nearest);
                if(hit&&*hit<=nearest){nearest=*hit;selectedTask=task.id;selectedFile=file.id;}
            }
            if(!selectedFile.empty()&&!submitTaskIntent({snapshot.ack+1,snapshot.revision,TaskAction::SelectFile,selectedTask,selectedFile}))throw std::runtime_error("Could not select schematic placement");
        }
        return;
    }
    if(state.mode==ToolMode::Rebuild && button==2 && !primaryPick){
        auto projection=structure::detail::StructureSession::getInstance().loaded();
        if(!projection||!projection->independentFiles)throw std::runtime_error("Select a loaded schematic placement for Rebuild");
        structure::TaskProjectionView view{projection->independentFiles->files};
        auto eye=owner.getEyePos(),direction=owner.getViewVector(1.0f);
        auto hit=structure::traceTaskProjection(view,{eye.x,eye.y,eye.z},{direction.x,direction.y,direction.z},10);
        if(!hit)throw std::runtime_error("No schematic block within Rebuild reach");
        auto worldHit=targetedWorldBlock(owner,10);
        if(worldHit && worldHit->distance<hit->distance)throw std::runtime_error("A world block is in front of the schematic");
        // The new block goes in the cell in front of the targeted face, inside the same source subregion.
        auto target=structure::schematicEditCell(view,*hit,hit->enteredFace);
        if(!target)throw std::runtime_error("The cell in front of that face is outside the source subregion");
        auto const& file=view.files()[target->file];
        auto const& primary=primaryBlock();
        auto stored=sourceStateFor(primary,file.rotation,file.mirror);
        if(!stored)throw std::runtime_error("Could not store "+primary.getTypeName()+" in the rotated or mirrored placement exactly");
        std::string error;
        if(!editLoadedSchematic(file.loaded,target->regionName,target->regionCell,*stored,error))throw std::runtime_error(error);
        return;
    }
    if(state.mode==ToolMode::Rebuild && button==1 && !primaryPick){
        auto projection=structure::detail::StructureSession::getInstance().loaded();
        if(!projection||!projection->independentFiles)throw std::runtime_error("Select a loaded schematic placement for Rebuild");
        structure::TaskProjectionView view{projection->independentFiles->files};
        auto eye=owner.getEyePos(),direction=owner.getViewVector(1.0f);
        auto hit=structure::traceTaskProjection(view,{eye.x,eye.y,eye.z},
            {direction.x,direction.y,direction.z},10);
        if(!hit)throw std::runtime_error("No schematic block within Rebuild reach");
        auto worldHit=targetedWorldBlock(owner,10);
        if(worldHit && worldHit->distance<hit->distance)
            throw std::runtime_error("A world block is in front of the schematic");
        auto target=structure::schematicEditCell(view,*hit);
        if(!target)throw std::runtime_error("Could not identify the source subregion for this block");
        auto const& file=view.files()[target->file];
        std::string error;
        if(!editLoadedSchematic(file.loaded,target->regionName,target->regionCell,
                                structure::JavaBlockState{"minecraft:air",{}},error))
            throw std::runtime_error(error);
        return;
    }
    auto hitPoint=targetedPoint(owner,200);if(!hitPoint)throw std::runtime_error("No loaded world block within 200 blocks of the crosshair");
    Point point=*hitPoint;BlockPos const hitBlock{point.x,point.y,point.z};
    if(button==3){auto const& block=owner.getDimensionBlockSource().getBlock(hitBlock);
        if(primaryPick){state.primary=block.getTypeName();primaryData=block.mSerializationId->toBinaryNbt();}
        else{state.secondary=block.getTypeName();secondaryData=block.mSerializationId->toBinaryNbt();}return;}
    if(state.mode==ToolMode::SchematicPlacement||state.mode==ToolMode::PasteSchematic||state.mode==ToolMode::GridPaste){
        auto snapshot=structure::detail::StructureSession::getInstance().snapshot();
        if(!snapshot.editor)throw std::runtime_error("Select a schematic placement first with middle-click");
        auto& session=structure::detail::StructureSession::getInstance();session.setOffsetX(point.x);session.setOffsetY(point.y);session.setOffsetZ(point.z);return;
    }
    if(state.mode==ToolMode::Move&&button==2){state.moveTarget=position(point);return;}
    if(state.mode==ToolMode::Rebuild)throw std::runtime_error("Schematic rebuild edits require the virtual-world editor");
    auto draft=structure::capture::getSnapshot().draft;
    if(draft.normalMode&&!selectedRegion(draft))addRegion(draft,"");
    setToolCorner(draft,point,button==2);structure::capture::updateDraft(draft);
}
}
bool queueToolCommand(std::string const& command,std::string const& value){std::lock_guard lock(commandMutex);if(commands.size()>=16)return false;commands.emplace_back(command,value);return true;}
ToolStatus toolStatus(){return *published.load();}
bool toolMouseInput(int button,bool down){
    if(button<1||button>3)return false;auto mask=1u<<button;
    if(!down)return (heldButtons.fetch_and(~mask)&mask)!=0;
    if(!armed.load())return false;heldButtons.fetch_or(mask);mouse.store(button);return true;
}
bool toolWheelInput(short delta,bool nudge){if(!armed.load()||delta==0)return false;return queueToolCommand(nudge?"nudge":"cycle",nudge?(delta<0?"back":""):(delta>0?"back":""));}
bool toolKeyInput(unsigned value){
    if(javaConfigReady())return false;
    auto& ui=structure::detail::StructureUiState::getInstance();
    for(std::size_t slot=input::kJavaHotkeyFirst;slot<input::kHotkeyCount;++slot){
        auto binding=ui.inputHotkey(slot);if(!binding.key||binding.key!=value||binding.modifiers!=ui.currentHotkeyModifiers())continue;
        if(!ui.tryPressHotkey(slot))return true;
        auto id=static_cast<input::HotkeyId>(slot);
        switch(id){
        case input::HotkeyId::ExecuteOperation:queueToolCommand("execute");break;
        case input::HotkeyId::NudgeSelectionNegative:queueToolCommand("nudge","back");break;
        case input::HotkeyId::NudgeSelectionPositive:queueToolCommand("nudge");break;
        case input::HotkeyId::AddSelectionBox:queueToolCommand("selection","addRegion");break;
        case input::HotkeyId::DeleteSelectionBox:queueToolCommand("selection","removeRegion");break;
        case input::HotkeyId::SetAreaOrigin:queueToolCommand("originPlayer");break;
        case input::HotkeyId::SetSelectionPosition1:queueToolCommand("corner1Player");break;
        case input::HotkeyId::SetSelectionPosition2:queueToolCommand("corner2Player");break;
        case input::HotkeyId::LayerModeNext:ui.queueLayerModeDelta(1);break;
        case input::HotkeyId::LayerModePrevious:ui.queueLayerModeDelta(-1);break;
        case input::HotkeyId::SelectionCornersMode:queueToolCommand("selection","cornerMode");break;
        default:break;
        }
        return true;
    }
    return false;
}
bool toolCommand(std::string const& command,std::string const& value){
    auto* owner=player();state.error.clear();
    try{
        if(command=="cycle"){
            if(state.running)throw std::runtime_error("A tool operation is running");
            state.mode=nextToolMode(state.mode,(value=="back")!=(javaConfigReady()&&javaToolOptions()->reverseCycle));
        }else if(command=="execute"){if(!owner)throw std::runtime_error("No active world");prepareOperation(*owner);}
        else if(command=="cancel"){plans.clear();gridTargets.clear();operation=0;state.running=false;resetStructureWriteActions();}
        else if(command=="mouse"){if(!owner)throw std::runtime_error("No active world");int button=value=="1"?1:value=="2"?2:value.starts_with("3 ")?3:0;if(!button)throw std::invalid_argument("Invalid tool button");click(*owner,button,value=="3 primary"?1:value=="3 secondary"?2:0);}
        else if(command=="selection"){
            if(!owner)throw std::runtime_error("No active world");
            structure::capture::selectionCommand(value);
        }else if(command=="originPlayer"||command=="corner1Player"||command=="corner2Player"){
            if(!owner)throw std::runtime_error("No active world");
            if(command=="originPlayer"){
                auto point=owner->getFeetBlockPos();auto draft=structure::capture::getSnapshot().draft;draft.origin=Point{point.x,point.y,point.z};draft.selectedCorner=3;structure::capture::updateDraft(draft);
            }else structure::capture::setPointFromPlayer(command=="corner1Player"?PointSlot::First:PointSlot::Second);
        }
        else if(command=="nudge"){
            if(!owner)throw std::runtime_error("No active world");
            int amount=value=="back"?-1:1;auto direction=owner->getViewVector(1.0f);std::array<float,3> view{direction.x,direction.y,direction.z};
            auto axis=std::max_element(view.begin(),view.end(),[](float a,float b){return std::abs(a)<std::abs(b);})-view.begin();
            std::array<int,3> delta{};delta[axis]=(view[axis]<0?-1:1)*amount;
            if(state.mode==ToolMode::SchematicPlacement||state.mode==ToolMode::PasteSchematic||state.mode==ToolMode::GridPaste||state.mode==ToolMode::Rebuild){
                // Queue the relative movement through the same session state consumed by placement persistence.
                structure::detail::StructureUiState::getInstance().queueOffsetDelta(delta[0],delta[1],delta[2]);
            }else{auto draft=structure::capture::getSnapshot().draft;nudgeSelectedElement(draft,delta);structure::capture::updateDraft(draft);}
        }
        else if(command=="primary"||command=="secondary"){
            if(!owner||state.running)throw std::runtime_error("Block choice requires an idle tool in a world");
            auto block=Block::tryGetFromRegistry(HashedString(value));if(!block)throw std::invalid_argument("Unknown block identifier: "+value);
            if(command=="primary"){state.primary=block->getTypeName();primaryData=block->mSerializationId->toBinaryNbt();}
            else{state.secondary=block->getTypeName();secondaryData=block->mSerializationId->toBinaryNbt();}
        }
        else if(command=="moveTarget"){
            if(!owner||state.running)throw std::runtime_error("Move destination requires an idle tool in a world");
            std::istringstream input(value);std::array<int,3> point;std::string extra;
            if(!(input>>point[0]>>point[1]>>point[2])||(input>>extra))throw std::invalid_argument("Invalid move destination");
            state.moveTarget=point;
        }
        else if(command=="grid"){std::istringstream in(value);int axis,negative,positive,step;if(!(in>>axis>>negative>>positive>>step)||axis<0||axis>2||negative<0||positive<0||negative>32||positive>32||step<0)throw std::runtime_error("Invalid grid settings");state.gridNegative[axis]=negative;state.gridPositive[axis]=positive;state.gridStep[axis]=step;}
        else throw std::runtime_error("Unknown tool operation");
    }catch(std::exception const& error){if(command=="execute"){plans.clear();gridTargets.clear();state.running=false;}state.error=error.what();publish();return false;}
    publish();return true;
}
void processToolActions(){
    auto* owner=player();auto revision=detail::taskWorldRevision();
    auto dimension=owner?std::optional<int>(static_cast<int>(owner->getDimensionId())):std::nullopt;
    if(revision!=worldRevision||dimension!=toolDimension){toolDimension=dimension;mouse=0;key=0;heldButtons=0;armed=false;plans.clear();gridTargets.clear();operation=0;state.running=false;state.moveTarget.reset();worldRevision=revision;primaryData.clear();secondaryData.clear();state.error.clear();publish();}
    {std::deque<std::pair<std::string,std::string>> requested;{std::lock_guard lock(commandMutex);requested.swap(commands);}for(auto const& [command,value]:requested)toolCommand(command,value);}
    auto toolOptions=javaToolOptions();
    bool held=owner&&(!javaConfigReady()||(toolOptions->enabled&&toolOptions->rendering))&&owner->getSelectedItem().getTypeName()==toolOptions->item;
    bool creative=owner&&owner->isCreative();
    bool localWriteAvailable=owner&&structureWriteStatus().localAvailable;
    bool companionAvailable=owner&&structure::write::command::realmCompanionEnabled();
    if(held!=state.held||creative!=state.creative||localWriteAvailable!=state.localWriteAvailable||companionAvailable!=state.companionAvailable){
        state.held=held;state.creative=creative;state.localWriteAvailable=localWriteAvailable;state.companionAvailable=companionAvailable;publish();
    }
    // Input's HUD-only context gate is checked again at the platform hook.
    armed.store(held);
    try{
        if(owner){if(auto button=mouse.exchange(0)) {click(*owner,button);publish();}if(key.exchange(0))toolCommand("execute");}
        if(!state.running)return;
        auto writing=structureWriteStatus();
        if(operation){
            if(writing.ack<operation||writing.busy)return;
            if(writing.result!=WriteResult::Complete){state.error=writing.detail.empty()?"Tool operation failed":writing.detail;plans.clear();gridTargets.clear();state.running=false;publish();return;}
            ++state.done;operation=0;publish();
        }
        if(plans.empty()){
            if(moveSource&&moveDestination&&structure::capture::getSnapshot().draft==*moveSource)structure::capture::updateDraft(*moveDestination);
            moveSource.reset();moveDestination.reset();state.running=false;publish();return;
        }
        if(!owner||(!owner->isCreative()&&!writing.localAvailable&&!structure::write::command::realmCompanionEnabled()))
            throw std::runtime_error("Survival tool writes require an integrated local world; remote writes need server command permission");
        if(writing.busy)return;
        StructureWriteIntent request;request.id=writing.ack+1;request.worldRevision=revision;
        request.mode=writing.localAvailable?WriteMode::LocalWorld:WriteMode::ServerCommands;
        if(!gridTargets.empty()){request.prepared=std::make_shared<StructureWritePlan>(*plans.front());request.prepared->target=gridTargets.front();}
        else request.prepared=plans.front();
        request.preparedName="Litematica tool";
        if(submitStructureWrite(request)){operation=request.id;if(gridTargets.empty())plans.pop_front();else{gridTargets.pop_front();if(gridTargets.empty())plans.pop_front();}}
    }catch(std::exception const& error){plans.clear();gridTargets.clear();state.running=false;state.error=error.what();publish();}
}
void resetToolActions(){armed=false;mouse=0;key=0;heldButtons=0;plans.clear();gridTargets.clear();operation=0;state.running=false;state.moveTarget.reset();publish();}
}
