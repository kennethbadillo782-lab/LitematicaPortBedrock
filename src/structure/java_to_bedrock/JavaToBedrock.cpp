// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The mapping data consumed here is generated from Chunker (MIT) and targets
// Bedrock 1.26.20. See THIRD_PARTY_NOTICES.md and tools/java_to_bedrock/.

#include <fstream>
#include <map>
#include <set>
#include <cstdlib>
#include "structure/java_to_bedrock/JavaToBedrock.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <zlib.h>

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/deps/nbt/Tag.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/registry/BlockTypeRegistry.h"
#include "mc/world/level/material/Material.h"

namespace lholo::structure {
namespace {

using StatePairs = std::vector<std::pair<std::string, std::string>>;

#include "structure/java_to_bedrock/GeneratedChunkerMappings.inc"

struct Mapping {
    int         minDataVersion{};
    std::string bedrockName;
    StatePairs  bedrockStates;
    bool        waterlogged{};
};

using MappingTable = std::unordered_map<std::string, std::vector<Mapping>>;

std::string canonicalProperties(StatePairs properties) {
    std::ranges::sort(properties);
    std::string result;
    for (auto const& [key, value] : properties) {
        if (!result.empty()) result.push_back(',');
        result.append(key).push_back('=');
        result.append(value);
    }
    return result;
}

StatePairs parseStates(std::string_view text, bool& waterlogged) {
    StatePairs states;
    while (!text.empty()) {
        auto const comma = text.find(',');
        auto const item = text.substr(0, comma);
        auto const equal = item.find('=');
        if (equal != std::string_view::npos) {
            std::string key{item.substr(0, equal)};
            std::string value{item.substr(equal + 1)};
            if (key == "waterlogged") {
                waterlogged = value == "true";
            } else {
                // Chunker represents byte states as booleans; the game registry
                // serializes those same states as 0/1 ByteTags.
                if (value == "true") value = "1";
                else if (value == "false") value = "0";
                states.emplace_back(std::move(key), std::move(value));
            }
        }
        if (comma == std::string_view::npos) break;
        text.remove_prefix(comma + 1);
    }
    return states;
}

MappingTable loadMappings() {
    std::string table(kChunkerMappingRawSize, '\0');
    uLongf outputSize = static_cast<uLongf>(table.size());
    auto const result = uncompress(
        reinterpret_cast<Bytef*>(table.data()),
        &outputSize,
        kChunkerMappingCompressed,
        static_cast<uLong>(sizeof(kChunkerMappingCompressed))
    );
    if (result != Z_OK || outputSize != table.size()) {
        throw std::runtime_error("Could not decompress generated Chunker block mappings");
    }

    MappingTable mappings;
    std::string_view remaining{table};
    while (!remaining.empty()) {
        auto const newline = remaining.find('\n');
        auto const line = remaining.substr(0, newline);
        if (!line.empty() && line.front() != '#') {
            std::string_view columns[5];
            auto rest = line;
            for (int i = 0; i < 4; ++i) {
                auto const tab = rest.find('\t');
                columns[i] = rest.substr(0, tab);
                rest.remove_prefix(tab + 1);
            }
            columns[4] = rest;

            Mapping mapping;
            std::from_chars(
                columns[0].data(), columns[0].data() + columns[0].size(), mapping.minDataVersion
            );
            mapping.bedrockName = columns[3];
            mapping.bedrockStates = parseStates(columns[4], mapping.waterlogged);

            std::string key{columns[1]};
            key.push_back('\0');
            key.append(columns[2]);
            mappings[std::move(key)].push_back(std::move(mapping));
        }
        if (newline == std::string_view::npos) break;
        remaining.remove_prefix(newline + 1);
    }
    return mappings;
}

MappingTable const& mappingTable() {static auto const mappings=loadMappings();return mappings;}
Mapping const* findMapping(
    std::string const& javaName,
    StatePairs const&  properties,
    int                javaDataVersion
) {
    auto const& mappings = mappingTable();
    std::string key = javaName;
    key.push_back('\0');
    key.append(canonicalProperties(properties));
    auto const found = mappings.find(key);
    if (found == mappings.end()) return nullptr;

    auto const& versions = found->second;
    Mapping const* selected = &versions.front();
    for (auto const& version : versions) {
        if (javaDataVersion > 0 && version.minDataVersion > javaDataVersion) break;
        selected = &version;
    }
    return selected;
}

struct PermutationTable {
    std::vector<std::pair<StatePairs, Block const*>> permutations;
};

std::mutex                                        gCacheMutex;
std::unordered_map<std::string, PermutationTable> gPermutationCache;
Block const*                                      gWaterSource{};

StatePairs readBlockStates(Block const& block) {
    StatePairs states;
    for (auto const& [key, value] : block.mSerializationId.get()) {
        if (key != "states" || !value.hold<::CompoundTag>()) continue;
        for (auto const& [stateKey, stateValue] : value.get<::CompoundTag>()) {
            auto const type = stateValue.getId();
            if (type == ::Tag::Type::Byte) {
                states.emplace_back(
                    stateKey, std::to_string(static_cast<int>(stateValue.get<::ByteTag>().data))
                );
            } else if (type == ::Tag::Type::Int) {
                states.emplace_back(stateKey, std::to_string(stateValue.get<::IntTag>().data));
            } else if (type == ::Tag::Type::String) {
                states.emplace_back(stateKey, static_cast<std::string const&>(stateValue.get<::StringTag>()));
            }
        }
        break;
    }
    return states;
}

PermutationTable const& permutationsFor(std::string const& name) {
    auto const cached = gPermutationCache.find(name);
    if (cached != gPermutationCache.end()) return cached->second;

    PermutationTable table;
    auto const defaultBlock = Block::tryGetFromRegistry(HashedString(name));
    if (defaultBlock && defaultBlock->getTypeName() == name) {
        for (auto const& permutation : defaultBlock->getBlockType().mBlockPermutations.get()) {
            if (!permutation) continue;
            table.permutations.emplace_back(readBlockStates(*permutation), permutation.get());
        }
    }
    return gPermutationCache.emplace(name, std::move(table)).first->second;
}

Block const* resolvePermutation(PermutationTable const& table, StatePairs const& requested) {
    for (auto const& [states, block] : table.permutations) {
        bool matches = std::ranges::all_of(requested, [&](auto const& requestedState) {
            return std::ranges::find(states, requestedState) != states.end();
        });
        if (matches) return block;
    }
    return nullptr;
}

Block const* waterSource() {
    if (!gWaterSource) {
        auto const water = Block::tryGetFromRegistry(HashedString("minecraft:water"));
        if (water && !water->isAir()) gWaterSource = &*water;
    }
    return gWaterSource;
}

Block const* resolveExactBedrockBlock(std::string const& bedrockName) {
    if (bedrockName.empty()) return nullptr;
    auto const block = Block::tryGetFromRegistry(HashedString(bedrockName));
    if (block && block->getTypeName() == bedrockName && !block->isAir()) return &*block;
    return nullptr;
}

} // namespace

ResolvedJavaBlock resolveJavaBlockState(
    std::string const&                                      javaName,
    std::vector<std::pair<std::string, std::string>> const& properties,
    int                                                     javaDataVersion
) {
    auto const* mapping = findMapping(javaName, properties, javaDataVersion);
    if (!mapping) {
        std::lock_guard lock(gCacheMutex);
        auto const* resolved = resolveExactBedrockBlock(javaName);
        if (!resolved || resolved->isAir()) return {};

        ResolvedJavaBlock result{.mapped = true};
        if (resolved->getTypeName() == "minecraft:bubble_column") {
            result.block = resolved;
            result.liquid = waterSource();
        } else if (resolved->getBlockType().mMaterial->mLiquid) {
            result.liquid = resolved;
        } else {
            result.block = resolved;
        }
        return result;
    }
    if (mapping->bedrockName == "minecraft:air") return {.mapped = true};

    std::lock_guard lock(gCacheMutex);
    auto const* resolved = resolvePermutation(
        permutationsFor(mapping->bedrockName), mapping->bedrockStates
    );
    if (!resolved || resolved->isAir()) {
        resolved = resolveExactBedrockBlock(mapping->bedrockName);
    }
    if (!resolved || resolved->isAir()) return {};

    ResolvedJavaBlock result{.mapped = true};
    if (resolved->getTypeName() == "minecraft:bubble_column") {
        result.block = resolved;
        if (mapping->waterlogged) result.liquid = waterSource();
    } else if (resolved->getBlockType().mMaterial->mLiquid) {
        result.liquid = resolved;
    } else {
        result.block = resolved;
        if (mapping->waterlogged) result.liquid = waterSource();
    }
    return result;
}

std::optional<JavaBlockState> exportJavaBlockState(Block const& block,bool waterlogged) {
    if(block.isAir()) return JavaBlockState{"minecraft:air",{}};
    struct Candidate {std::string key;Mapping mapping;};
    static auto const reverse=[] {
        std::unordered_map<std::string,std::vector<Candidate>> result;
        for(auto const& [key,versions]:mappingTable()) if(!versions.empty()) {
            auto const& mapping=*std::max_element(versions.begin(),versions.end(),[](auto const& a,auto const& b){return a.minDataVersion<b.minDataVersion;});
            result[mapping.bedrockName].push_back({key,mapping});
        }
        for(auto& [name,entries]:result) std::sort(entries.begin(),entries.end(),[](auto const& a,auto const& b){
            if(a.mapping.minDataVersion!=b.mapping.minDataVersion)return a.mapping.minDataVersion>b.mapping.minDataVersion;
            if(a.mapping.bedrockStates.size()!=b.mapping.bedrockStates.size())return a.mapping.bedrockStates.size()>b.mapping.bedrockStates.size();
            return a.key<b.key;
        });
        return result;
    }();
    auto found=reverse.find(block.getTypeName());
    if(found==reverse.end())return {};
    for(auto const& candidate:found->second) {
        if(candidate.mapping.waterlogged!=waterlogged)continue;
        auto split=candidate.key.find('\0');JavaBlockState result{candidate.key.substr(0,split),{}};
        std::string_view properties{candidate.key.data()+split+1,candidate.key.size()-split-1};
        while(!properties.empty()) {
            auto comma=properties.find(',');auto pair=properties.substr(0,comma);auto equal=pair.find('=');
            if(equal!=std::string_view::npos)result.properties.emplace_back(pair.substr(0,equal),pair.substr(equal+1));
            if(comma==std::string_view::npos)break;properties.remove_prefix(comma+1);
        }
        auto resolved=resolveJavaBlockState(result.name,result.properties,javaExportDataVersion());
        auto* restored=resolved.block?resolved.block:resolved.liquid;
        if(!restored||restored->mSerializationIdHash!=block.mSerializationIdHash)continue;
        if(waterlogged&&(!resolved.block||!resolved.liquid))continue;
        return result;
    }
    return {};
}
namespace {
using NeighborFn=std::function<Block const*(int,int,int)>;
bool endsWith(std::string const& s,char const* suffix){return s.ends_with(suffix);}
// Name Java uses for a Bedrock block: the Java side of the reverse mapping when one exists.
std::string javaNameFor(Block const& block){
    static std::unordered_map<std::string,std::string> cache;static std::mutex mutex;
    std::lock_guard lock(mutex);
    auto bedrock=block.getTypeName();
    if(auto cached=cache.find(bedrock);cached!=cache.end())return cached->second;
    std::string found=bedrock;
    if(mappingTable().contains(bedrock+'\0'))return cache.emplace(bedrock,bedrock).first->second;
    for(auto const& [key,versions]:mappingTable()){
        if(versions.empty()||versions.front().bedrockName!=bedrock)continue;
        found=key.substr(0,key.find('\0'));break;
    }
    return cache.emplace(bedrock,found).first->second;
}
bool fullSolid(Block const* other){
    if(!other||other->isAir())return false;
    auto name=other->getTypeName();
    if(name.find("leaves")!=std::string::npos||name.find("stairs")!=std::string::npos||name.find("slab")!=std::string::npos
        ||name.find("door")!=std::string::npos||name.find("trapdoor")!=std::string::npos||name.find("shulker")!=std::string::npos
        ||name.find("fence")!=std::string::npos||name.find("pane")!=std::string::npos||name.find("wall")!=std::string::npos
        ||name.find("carpet")!=std::string::npos||name.find("button")!=std::string::npos||name.find("torch")!=std::string::npos)return false;
    return other->getBlockType().mMaterial->mSolid;
}
}
std::optional<JavaBlockState> exportJavaBlockStateApproximate(Block const& block,bool waterlogged,NeighborFn const& neighbor,bool* computed) {
    if(computed)*computed=false;
    auto name=block.getTypeName();
    static std::set<std::string> const bedrockOnly{"minecraft:client_request_placeholder_block"};
    if(bedrockOnly.contains(name))return {};
    // Java has no sulfur spike; a pointed dripstone tip is the closest shape (documented substitution).
    if(name=="minecraft:sulfur_spike"){
        if(computed)*computed=true;
        return JavaBlockState{"minecraft:pointed_dripstone",{{"thickness","tip"},{"vertical_direction","up"},{"waterlogged",waterlogged?"true":"false"}}};
    }
    JavaBlockState result{javaNameFor(block),{}};
    bool isFence=endsWith(name,"_fence")&&!endsWith(name,"_fence_gate"),isPane=endsWith(name,"_pane")||name=="minecraft:iron_bars";
    if(endsWith(name,"_stairs")&&neighbor){
        static char const* const dirs[]{"east","south","west","north"};static int const dx[]{1,0,-1,0},dz[]{0,1,0,-1};
        auto read=[&](Block const* b,int& facing,bool& top)->bool{
            if(!b||!b->getTypeName().ends_with("_stairs"))return false;
            facing=-1;top=false;
            for(auto const& [key,value]:readBlockStates(*b)){
                if(key=="weirdo_direction"){static int const toIndex[]{0,2,1,3};int v=std::atoi(value.c_str());if(v>=0&&v<4)facing=toIndex[v];} // 0 east,1 west,2 south,3 north -> clockwise index
                if(key=="upside_down_bit")top=value=="1";
            }
            return facing>=0;
        };
        int facing=3;bool top=false;if(!read(&block,facing,top)){facing=3;top=false;} // unreadable states keep the Java defaults (north, bottom)
        auto stairAt=[&](int direction,int& f,bool& t){return read(neighbor(dx[direction],0,dz[direction]),f,t);};
        auto canTake=[&](int direction){int f;bool t;return !(stairAt(direction,f,t)&&f==facing&&t==top);};
        std::string shape="straight";int f;bool t;
        if(stairAt(facing,f,t)&&t==top&&(f&1)!=(facing&1)&&canTake((f+2)&3))shape=f==((facing+3)&3)?"outer_left":"outer_right";
        else if(stairAt((facing+2)&3,f,t)&&t==top&&(f&1)!=(facing&1)&&canTake(f))shape=f==((facing+3)&3)?"inner_left":"inner_right";
        result.properties={{"facing",dirs[facing]},{"half",top?"top":"bottom"},{"shape",shape},{"waterlogged",waterlogged?"true":"false"}};
        // The Java name comes from the verified reverse mapping, so the computed state is returned without a registry round trip.
        if(computed)*computed=true;
        return result;
    }
    if((isFence||isPane)&&neighbor){
        static char const* const dirs[]{"north","east","south","west"};static int const dx[]{0,1,0,-1},dz[]{-1,0,1,0};
        for(int i=0;i<4;++i){
            auto* other=neighbor(dx[i],0,dz[i]);bool connects=false;
            if(other&&!other->isAir()){
                auto otherName=other->getTypeName();
                bool nether=name.find("nether_brick")!=std::string::npos;
                if(isFence)connects=(endsWith(otherName,"_fence")&&(otherName.find("nether_brick")!=std::string::npos)==nether)||endsWith(otherName,"_fence_gate")||fullSolid(other);
                else connects=endsWith(otherName,"_pane")||otherName=="minecraft:iron_bars"||endsWith(otherName,"_wall")||fullSolid(other);
            }
            result.properties.emplace_back(dirs[i],connects?"true":"false");
        }
        result.properties.emplace_back("waterlogged",waterlogged?"true":"false");
        if(computed)*computed=true;
        return result;
    }
    // An exact reverse mapping is not an approximation.
    if(auto exact=exportJavaBlockState(block,waterlogged)){if(computed)*computed=true;return exact;}
    if(waterlogged)if(auto dry=exportJavaBlockState(block,false)){if(computed)*computed=true;return dry;}
    // Best Java state whose Bedrock states are all present on this block (hash-insensitive match).
    {
        static std::unordered_map<std::string,std::vector<std::pair<std::string,Mapping const*>>> index;static std::mutex indexMutex;
        std::lock_guard lock(indexMutex);
        if(index.empty())for(auto const& [key,versions]:mappingTable())if(!versions.empty()){
            auto const* mapping=&*std::max_element(versions.begin(),versions.end(),[](auto const& a,auto const& b){return a.minDataVersion<b.minDataVersion;});
            index[mapping->bedrockName].emplace_back(key,mapping);
        }
        auto blockStates=readBlockStates(block);
        Mapping const* best=nullptr;std::string bestKey;
        if(auto found=index.find(name);found!=index.end())for(auto const& [key,mapping]:found->second){
            bool subset=std::ranges::all_of(mapping->bedrockStates,[&](auto const& pair){return std::ranges::find(blockStates,pair)!=blockStates.end();});
            if(!subset)continue;
            bool better=!best||(mapping->waterlogged==waterlogged&&best->waterlogged!=waterlogged)
                ||(mapping->waterlogged==best->waterlogged&&mapping->bedrockStates.size()>best->bedrockStates.size());
            if(better){best=mapping;bestKey=key;}
        }
        if(!best){
            // No exact state (e.g. crop growth 1/3/5/6 vs Java's 4 ages): take the closest numeric state, non-numeric states must match.
            auto toNumber=[](std::string const& text,int& out){char* end=nullptr;long v=std::strtol(text.c_str(),&end,10);if(end==text.c_str()||*end)return false;out=static_cast<int>(v);return true;};
            int bestDistance=1<<30;
            if(auto found=index.find(name);found!=index.end())for(auto const& [key,mapping]:found->second){
                int distance=0;bool ok=true;
                for(auto const& [stateKey,stateValue]:mapping->bedrockStates){
                    auto actual=std::ranges::find_if(blockStates,[&](auto const& pair){return pair.first==stateKey;});
                    if(actual==blockStates.end()){ok=false;break;}
                    int a,b;
                    if(toNumber(stateValue,a)&&toNumber(actual->second,b))distance+=std::abs(a-b);
                    else if(stateValue!=actual->second){ok=false;break;}
                }
                if(!ok||mapping->waterlogged!=waterlogged)continue;
                if(distance<bestDistance){bestDistance=distance;best=mapping;bestKey=key;}
            }
        }
        if(best){
            auto split=bestKey.find('\0');JavaBlockState state{bestKey.substr(0,split),{}};
            std::string_view properties{bestKey.data()+split+1,bestKey.size()-split-1};
            while(!properties.empty()){
                auto comma=properties.find(',');auto pair=properties.substr(0,comma);auto equal=pair.find('=');
                if(equal!=std::string_view::npos)state.properties.emplace_back(pair.substr(0,equal),pair.substr(equal+1));
                if(comma==std::string_view::npos)break;properties.remove_prefix(comma+1);
            }
            if(computed)*computed=true;
            return state;
        }
    }
    // Last resort: same name and default Java state.
    // Never drop a block into air: keep the best Java name/state even when the registry cannot confirm it.
    auto check=resolveJavaBlockState(result.name,result.properties,javaExportDataVersion());
    if(!check.block&&!check.liquid)result.properties.clear();
    return result;
}
void writeJavaExportAudit(std::filesystem::path const& file) {
    struct Row {std::size_t total{},exact{},derived{},fallback{};std::string example;};
    std::map<std::string,Row> rows;
    auto& registry=BlockTypeRegistry::get();
    for(auto const& [key,type]:registry.mBlockLookupMap.get()){
        if(!type)continue;
        auto name=type->mNameInfo->mFullName->getString();
        for(auto const& permutation:type->mBlockPermutations.get()){
            if(!permutation)continue;
            Block const& block=*permutation;
            auto& row=rows[name];++row.total;
            if(block.isAir()||exportJavaBlockState(block,false)){++row.exact;continue;}
            bool computed=false;
            auto approximate=exportJavaBlockStateApproximate(block,false,[](int,int,int)->Block const*{return nullptr;},&computed);
            if(approximate&&computed){++row.derived;continue;}
            ++row.fallback;
            if(row.example.empty()){for(auto const& [stateKey,stateValue]:readBlockStates(block))row.example+=stateKey+"="+stateValue+" ";}
        }
    }
    std::ofstream out(file,std::ios::trunc);
    std::size_t bad=0,derivedBlocks=0;
    for(auto const& [name,row]:rows){if(row.fallback)++bad;if(row.derived)++derivedBlocks;}
    out<<"Bloques registrados: "<<rows.size()<<"\n";
    out<<"Bloques con algun estado SIN equivalente verificado en Java: "<<bad<<"\n";
    out<<"Bloques con estados resueltos por aproximacion numerica o vecinos: "<<derivedBlocks<<"\n\n";
    for(auto const& [name,row]:rows)if(row.fallback)out<<"SIN_TRADUCCION "<<name<<" estados="<<row.total<<" exactos="<<row.exact<<" derivados="<<row.derived<<" sin_verificar="<<row.fallback<<" ejemplo: "<<row.example<<"\n";
    out<<"\n";
    for(auto const& [name,row]:rows)if(!row.fallback&&row.derived)out<<"DERIVADO "<<name<<" estados="<<row.total<<" exactos="<<row.exact<<" derivados="<<row.derived<<"\n";
}
int javaExportDataVersion() {
    static int const version=[] {int result=0;for(auto const& [key,entries]:mappingTable())for(auto const& mapping:entries)result=std::max(result,mapping.minDataVersion);return result;}();
    return version;
}

void resetJavaBlockMappingCache() {
    std::lock_guard lock(gCacheMutex);
    gPermutationCache.clear();
    gWaterSource = nullptr;
}

} // namespace lholo::structure
