#include "structure/capture/LitematicWorldExport.h"
#include "structure/capture/SelectionModel.h"
#include "structure/capture/CaptureBounds.h"
#include "structure/formats/LitematicExport.h"
#include "structure/formats/JavaBlockEntityExport.h"
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/deps/nbt/ShortTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include <fstream>
#include <map>
#include <thread>
#include <Windows.h>
namespace lholo::structure::capture {
namespace {
// Reads one Bedrock sign side; absent fields keep the Bedrock defaults (black, not glowing).
BedrockSignSide readSignSide(CompoundTag const& sign, char const* name) {
    BedrockSignSide side;
    if (!sign.contains(name)) return side;
    auto const& tag = sign[name];
    if (!tag.hold<CompoundTag>()) throw std::runtime_error(std::string("Unexpected sign data in ") + name);
    auto const& compound = tag.get<CompoundTag>();
    if (compound.contains("Text") && compound["Text"].hold<StringTag>()) side.text = std::string(compound["Text"].get<StringTag>());
    if (compound.contains("SignTextColor") && compound["SignTextColor"].hold<IntTag>()) side.color = compound["SignTextColor"].get<IntTag>().data;
    if (compound.contains("IgnoreLighting") && compound["IgnoreLighting"].hold<ByteTag>()) side.glowing = compound["IgnoreLighting"].get<ByteTag>().data != 0;
    return side;
}

// Only block entities with an exact Java adapter are exported; failures are reported together after scanning.
JavaNbtTag exportBedrockBlockEntity(BlockActor const& actor, Block const& block, std::array<int, 3> position) {
    CompoundTag tag;
    if (!actor.save(tag, SaveContext{})) throw std::runtime_error("Could not capture block entity of " + block.getTypeName());
    std::string id;
    if (tag.contains("id") && tag["id"].hold<StringTag>()) id = std::string(tag["id"].get<StringTag>());
    auto readItems = [&](std::size_t slots) {
        std::vector<JavaContainerItem> items;
        if (!tag.contains("Items")) return items;
        auto const& value = tag["Items"];
        if (!value.hold<ListTag>()) throw std::runtime_error("Bedrock " + id + " Items is not a list");
        auto const& list = value.get<ListTag>();
        items.reserve(list.size());
        for (auto const& entry : list) {
            if (!entry.hold<CompoundTag>()) throw std::runtime_error("Bedrock " + id + " contains a malformed item");
            auto const& stack = entry.get<CompoundTag>();
            if (!stack.contains("Name", Tag::Type::String) || !stack.contains("Count", Tag::Type::Byte)
                || !stack.contains("Slot", Tag::Type::Byte))
                throw std::runtime_error("Bedrock " + id + " item is missing Name, Count or Slot");
            if (stack.contains("Damage")) {
                auto const& damage = stack["Damage"];
                auto const value = damage.hold<ShortTag>() ? damage.get<ShortTag>().data
                    : damage.hold<IntTag>() ? damage.get<IntTag>().data : -1;
                if (value != 0) throw std::runtime_error("Java export cannot convert damaged Bedrock items in " + id);
            }
            if (stack.contains("tag")
                && (!stack["tag"].hold<CompoundTag>() || !stack["tag"].get<CompoundTag>().empty()))
                throw std::runtime_error("Java export cannot convert Bedrock item custom data in " + id);
            auto const slot = static_cast<unsigned char>(stack["Slot"].get<ByteTag>().data);
            if (slot >= slots) throw std::runtime_error("Bedrock " + id + " item slot is out of range");
            items.push_back({
                static_cast<std::string const&>(stack["Name"].get<StringTag>()),
                stack["Count"].get<ByteTag>().data,
                stack["Slot"].get<ByteTag>().data,
            });
        }
        return items;
    };
    auto readString = [&](char const* key) {
        if (!tag.contains(key)) return std::string{};
        if (!tag[key].hold<StringTag>()) throw std::runtime_error("Bedrock " + id + " " + key + " is malformed");
        return static_cast<std::string const&>(tag[key].get<StringTag>());
    };
    // Bedrock "loot_tables/chests/x.json" is Java "minecraft:chests/x".
    auto applyLoot = [&](JavaNbtTag result) {
        if (!tag.contains("LootTable") || !tag["LootTable"].hold<StringTag>()) return result;
        std::string table = static_cast<std::string const&>(tag["LootTable"].get<StringTag>());
        if (table.rfind("loot_tables/", 0) == 0) table.erase(0, 12);
        if (table.size() > 5 && table.ends_with(".json")) table.resize(table.size() - 5);
        auto& fields = std::get<JavaNbtTag::Compound>(result.value);
        fields["LootTable"] = JavaNbtTag{table.find(':') == std::string::npos ? "minecraft:" + table : table};
        if (tag.contains("LootTableSeed") && tag["LootTableSeed"].hold<IntTag>())
            fields["LootTableSeed"] = JavaNbtTag{static_cast<std::int64_t>(tag["LootTableSeed"].get<IntTag>().data)};
        return result;
    };
    if (id == "PotentSulfurBlock") return JavaNbtTag{};  // runtime-only state: no Java tile entity
    if (id == "Bed" || id == "Beehive" || id == "Lectern") {
        auto result = exportJavaSimpleBlockEntity(id == "Bed" ? "minecraft:bed" : id == "Beehive" ? "minecraft:beehive" : "minecraft:lectern",
            position, {}, javaExportDataVersion());
        auto& fields = std::get<JavaNbtTag::Compound>(result.value);
        if (id == "Beehive") fields["bees"] = JavaNbtTag{JavaNbtTag::List{}};
        if (id == "Lectern" && tag.contains("book") && tag["book"].hold<CompoundTag>()) {
            auto const& book = tag["book"].get<CompoundTag>();
            if (book.contains("Name") && book["Name"].hold<StringTag>()) {
                JavaNbtTag::Compound item;
                item["id"] = JavaNbtTag{static_cast<std::string const&>(book["Name"].get<StringTag>())};
                item["count"] = JavaNbtTag{static_cast<std::int32_t>(1)};
                fields["Book"] = JavaNbtTag{std::move(item)};
                std::int32_t page = tag.contains("page") && tag["page"].hold<IntTag>() ? tag["page"].get<IntTag>().data : 0;
                fields["Page"] = JavaNbtTag{page};
            }
        }
        return result;
    }
    if (id == "BrewingStand") {
        auto items = readItems(5);
        // Bedrock: 0 ingredient, 1-3 bottles, 4 fuel. Java: 0-2 bottles, 3 ingredient, 4 fuel.
        for (auto& item : items) item.slot = item.slot == 0 ? 3 : item.slot <= 3 ? static_cast<std::int8_t>(item.slot - 1) : item.slot;
        std::vector<std::pair<std::string, std::int16_t>> brew;
        if (tag.contains("CookTime") && tag["CookTime"].hold<ShortTag>()) brew.emplace_back("BrewTime", tag["CookTime"].get<ShortTag>().data);
        auto result = exportJavaContainer("minecraft:brewing_stand", items, 5, position, readString("CustomName"), {}, brew, -1, javaExportDataVersion());
        if (tag.contains("FuelAmount") && tag["FuelAmount"].hold<ShortTag>())
            std::get<JavaNbtTag::Compound>(result.value)["Fuel"] = JavaNbtTag{static_cast<std::int8_t>(tag["FuelAmount"].get<ShortTag>().data)};
        return result;
    }
    if (id == "MobSpawner") {
        JavaNbtTag::Compound fields;
        fields["id"] = JavaNbtTag{std::string{"minecraft:mob_spawner"}};
        fields["x"] = JavaNbtTag{static_cast<std::int32_t>(position[0])};
        fields["y"] = JavaNbtTag{static_cast<std::int32_t>(position[1])};
        fields["z"] = JavaNbtTag{static_cast<std::int32_t>(position[2])};
        auto readShort = [&](char const* key, std::int16_t fallback) {
            if (!tag.contains(key)) return fallback;
            auto const& value = tag[key];
            return value.hold<ShortTag>() ? value.get<ShortTag>().data : value.hold<IntTag>() ? static_cast<std::int16_t>(value.get<IntTag>().data) : fallback;
        };
        fields["Delay"] = JavaNbtTag{readShort("Delay", 20)};
        fields["MinSpawnDelay"] = JavaNbtTag{readShort("MinSpawnDelay", 200)};
        fields["MaxSpawnDelay"] = JavaNbtTag{readShort("MaxSpawnDelay", 800)};
        fields["SpawnCount"] = JavaNbtTag{readShort("SpawnCount", 4)};
        fields["MaxNearbyEntities"] = JavaNbtTag{readShort("MaxNearbyEntities", 6)};
        fields["RequiredPlayerRange"] = JavaNbtTag{readShort("RequiredPlayerRange", 16)};
        fields["SpawnRange"] = JavaNbtTag{readShort("SpawnRange", 4)};
        auto entity = readString("EntityIdentifier");
        if (!entity.empty()) {
            JavaNbtTag::Compound inner;
            inner["id"] = JavaNbtTag{entity.find(':') == std::string::npos ? "minecraft:" + entity : entity};
            JavaNbtTag::Compound data;
            data["entity"] = JavaNbtTag{std::move(inner)};
            fields["SpawnData"] = JavaNbtTag{std::move(data)};
        }
        return JavaNbtTag{std::move(fields)};
    }
    if (id == "Bell" || id == "EnderChest" || id == "EnchantTable") {
        auto javaId = id == "Bell" ? "minecraft:bell"
            : id == "EnderChest" ? "minecraft:ender_chest" : "minecraft:enchanting_table";
        return exportJavaSimpleBlockEntity(javaId, position,
            id == "EnchantTable" ? readString("CustomName") : std::string{}, javaExportDataVersion());
    }
    if (id == "Chest" || id == "TrappedChest" || id == "Furnace" || id == "BlastFurnace"
        || id == "Smoker" || id == "Hopper" || id == "Dispenser" || id == "Dropper" || id == "ChiseledBookshelf") {
        std::string javaId = id == "BlastFurnace" ? "minecraft:blast_furnace"
            : id == "TrappedChest" ? "minecraft:chest"
            : id == "ChiseledBookshelf" ? "minecraft:chiseled_bookshelf"
            : "minecraft:" + std::string{id == "Chest" ? "chest" : id == "Furnace" ? "furnace"
                : id == "Smoker" ? "smoker" : id == "Hopper" ? "hopper"
                : id == "Dispenser" ? "dispenser" : "dropper"};
        std::size_t slots = id == "Chest" || id == "TrappedChest" ? 27
            : id == "ChiseledBookshelf" ? 6
            : id == "Hopper" ? 5 : id == "Dispenser" || id == "Dropper" ? 9 : 3;
        if (id == "ChiseledBookshelf" && (!tag.contains("last_interacted_slot") || !tag["last_interacted_slot"].hold<IntTag>()))
            throw std::runtime_error("Bedrock chiseled bookshelf has no valid last_interacted_slot");
        std::int32_t lastInteractedSlot = id == "ChiseledBookshelf" ? tag["last_interacted_slot"].get<IntTag>().data : -1;
        if (lastInteractedSlot < -1 || lastInteractedSlot >= 6)
            throw std::runtime_error("Bedrock chiseled bookshelf last_interacted_slot is out of range");
        std::vector<std::pair<std::string, std::int16_t>> timers;
        for (char const* key : {"BurnTime", "CookTime", "CookTimeTotal"}) if (tag.contains(key)) {
            auto const& timer = tag[key];
            int value = timer.hold<ShortTag>() ? timer.get<ShortTag>().data
                : timer.hold<IntTag>() ? timer.get<IntTag>().data
                : timer.hold<ByteTag>() ? timer.get<ByteTag>().data : -1;
            if (value < 0 || value > INT16_MAX)
                throw std::runtime_error("Java export cannot represent Bedrock " + id + " timer " + key);
            timers.emplace_back(key, static_cast<std::int16_t>(value));
        }
        if (id != "Furnace" && id != "BlastFurnace" && id != "Smoker" && !timers.empty())
            throw std::runtime_error("Bedrock " + id + " unexpectedly contains furnace timers");
        return applyLoot(exportJavaContainer(javaId, readItems(slots), slots, position, readString("CustomName"), readString("Lock"), timers, lastInteractedSlot, javaExportDataVersion()));
    }
    if (id == "Sign" || id == "HangingSign") {
        BedrockSign sign;
        sign.hanging = id == "HangingSign";
        sign.front = readSignSide(tag, "FrontText");
        sign.back = readSignSide(tag, "BackText");
        if (tag.contains("IsWaxed") && tag["IsWaxed"].hold<ByteTag>()) sign.waxed = tag["IsWaxed"].get<ByteTag>().data != 0;
        return exportJavaSign(sign, position, javaExportDataVersion());
    }
    if (id == "Barrel") {
        auto items = readItems(27);
        auto customName = readString("CustomName"), lock = readString("Lock");
        return applyLoot(exportJavaContainer("minecraft:barrel", items, 27, position, customName, lock, {}, -1, javaExportDataVersion()));
    }
    throw std::runtime_error("Java export needs a block-entity adapter for " + block.getTypeName() + " (" + id + ")");
}
}
bool exportLitematicWorld(BlockSource& world,Draft const& draft,std::filesystem::path const& path,std::string& error) try {
    if(world.mOwnerThreadID.get()!=std::this_thread::get_id())throw std::runtime_error("World export requires the BlockSource owner thread");
    if(draft.includeEntities)throw std::runtime_error("Java export cannot convert world entities yet; disable Include entities to export blocks and block entities.");
    {
        // One-off full audit of the running game's block registry against the Java translation (written next to the schematic).
        static bool audited=false;
        if(!audited){audited=true;try{writeJavaExportAudit(path.parent_path()/"auditoria_bloques_java.txt");}catch(...){}}
    }
    auto bounds=selectionBounds(draft);if(bounds.empty())throw std::runtime_error("No complete selection regions");
    auto origin=selectionOrigin(draft);
    std::vector<ExportRegion> regions;
    std::uint64_t totalCells{};
    std::uint64_t omittedChunkPlaceholders{};
    JavaExportIssues unsupported;
    std::map<std::string,std::uint32_t> valuePalette;
    auto dependsOnNeighbours=[](std::string const& name){return name.ends_with("_stairs")||(name.ends_with("_fence")&&!name.ends_with("_fence_gate"))||name.ends_with("_pane")||name=="minecraft:iron_bars";};
    for(std::size_t n=0;n<bounds.size();++n){
        auto const& box=bounds[n];auto size=captureSize(box);if(!size)throw std::runtime_error("Selection is too large");
        if(box.min.y<world.getMinHeight()||box.max.y>=world.getMaxHeight())throw std::runtime_error("Selection exceeds world height");
        if(!world.areChunksFullyLoaded({box.min.x,box.min.y,box.min.z},{box.max.x,box.max.y,box.max.z}))throw std::runtime_error("Selection includes unloaded chunks");
        ExportRegion region;region.name=draft.boxName;
        if(draft.normalMode){std::size_t i=0;for(auto const& subregion:draft.regions)if(subregion.enabled&&subregion.first&&subregion.second){if(i++==n){region.name=subregion.name;break;}}}
        region.size=*size;
        std::array<int,3> lo{box.min.x,box.min.y,box.min.z},base{origin.x,origin.y,origin.z};
        for(int a=0;a<3;++a){auto delta=static_cast<long long>(lo[a])-base[a];if(delta<INT32_MIN||delta>INT32_MAX)throw std::runtime_error("Region offset exceeds int32");region.position[a]=static_cast<int>(delta);}
        std::map<std::pair<Block const*,bool>,std::uint32_t> palette;
        region.palette.push_back({"minecraft:air",{}});
        auto volume=std::uint64_t((*size)[0])*(*size)[1]*(*size)[2];
        if(volume>detail::kMaximumJavaNbtSize/4-totalCells)throw std::runtime_error("Selection exceeds export memory limit");
        totalCells+=volume;
        region.indices.reserve(static_cast<std::size_t>(volume));
        for(int y=0;y<(*size)[1];++y)for(int z=0;z<(*size)[2];++z)for(int x=0;x<(*size)[0];++x){
            BlockPos position{box.min.x+x,box.min.y+y,box.min.z+z};
            auto const& block=world.getBlock(position);auto const& liquid=world.getExtraBlock(position);
            bool waterlogged=!block.isAir()&&!liquid.isAir();
            auto const& effective=block.isAir()&&!liquid.isAir()?liquid:block;
            if(effective.isAir()){region.indices.push_back(0);continue;}
            // This Bedrock-only internal marker is used while chunk data loads.
            // Java has no corresponding state, so serialize it as air and report
            // the omission instead of aborting an otherwise usable export.
            if(effective.getTypeName()=="minecraft:client_request_placeholder_block"){
                ++omittedChunkPlaceholders;
                region.indices.push_back(0);
                continue;
            }
            if(auto* actor=world.getBlockEntity(position)) {
                try {
                    auto entity=exportBedrockBlockEntity(*actor,effective,{position.x-box.min.x,position.y-box.min.y,position.z-box.min.z});
                    if(!std::holds_alternative<std::monostate>(entity.value))region.blockEntities.push_back(std::move(entity));
                } catch (std::exception const& exception) {
                    unsupported.record(std::string("Bloque con datos propios no exportado (se guarda el bloque sin ellos): ")+exception.what());
                }
            }
            auto key=std::make_pair(&effective,waterlogged);auto found=palette.find(key);
            if(found==palette.end()){
                bool computed=false;
                auto state=exportJavaBlockStateApproximate(effective,waterlogged,[&](int dx,int dy,int dz)->Block const*{return &world.getBlock({position.x+dx,position.y+dy,position.z+dz});},&computed);
                if(!state) {
                    unsupported.record("Sin equivalente Java, guardado como aire: " + effective.getTypeName());
                    region.indices.push_back(0);
                    continue;
                }
                if(!computed) unsupported.record("Estado aproximado (sin equivalente exacto) para " + effective.getTypeName());
                // States that depend on the neighbours (connections, stair shapes) differ per cell, so they are indexed by value, not by block type.
                if(computed&&dependsOnNeighbours(effective.getTypeName())){
                    std::string stateKey=state->name;for(auto const& [k,v]:state->properties)stateKey+="|"+k+"="+v;
                    auto cell=valuePalette.find(stateKey);
                    if(cell==valuePalette.end()){cell=valuePalette.emplace(stateKey,static_cast<std::uint32_t>(region.palette.size())).first;region.palette.push_back(std::move(*state));}
                    region.indices.push_back(cell->second);
                    continue;
                }
                auto index=static_cast<std::uint32_t>(region.palette.size());region.palette.push_back(std::move(*state));
                found=palette.emplace(key,index).first;
            }
            region.indices.push_back(found->second);
        }
        valuePalette.clear();
        regions.push_back(std::move(region));
    }
    if(!unsupported.empty()) {
        error=unsupported.warning();
        if(omittedChunkPlaceholders) error += "; además, " + std::to_string(omittedChunkPlaceholders) + " marcadores internos de carga guardados como aire";
    } else if(omittedChunkPlaceholders)
        error="Aviso: se exportó la estructura, pero se omitieron "+std::to_string(omittedChunkPlaceholders)+" minecraft:client_request_placeholder_block y se guardaron como aire porque son marcadores internos de carga de Bedrock sin equivalente Java.";
    auto bytes=encodeLitematic(regions,draft.name,"Litematica Bedrock","",javaExportDataVersion());
    auto temporary=path;temporary+=L".tmp";
    {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out.exceptions(std::ios::failbit|std::ios::badbit);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));out.close();}
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Could not commit litematic file");
    return true;
}catch(std::exception const& exception){error=exception.what();return false;}
}
