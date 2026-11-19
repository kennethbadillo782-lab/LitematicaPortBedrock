#pragma once
#include "structure/formats/JavaNbtWriter.h"
#include "structure/java_to_bedrock/JavaToBedrock.h"
#include <array>
#include <bit>
#include <chrono>
namespace lholo::structure {
struct ExportRegion {
    std::string name;
    std::array<int,3> position{},size{};
    std::vector<JavaBlockState> palette;
    std::vector<std::uint32_t> indices;
    std::vector<detail::JavaNbtTag> blockEntities;
};
inline std::string encodeLitematic(std::vector<ExportRegion> const& regions,std::string const& name,
    std::string const& author,std::string const& description,int dataVersion){
    using namespace detail;
    if(regions.empty())throw std::runtime_error("No selection regions to export");
    std::array<long long,3> min{},max{};bool first=true;
    std::uint64_t volume{},blocks{};
    for(auto const& region:regions){
        std::uint64_t count=1;
        for(int a=0;a<3;++a){if(region.size[a]<=0||count>detail::kMaximumJavaNbtSize/4/static_cast<unsigned>(region.size[a]))throw std::runtime_error("Invalid region size or export memory limit");count*=region.size[a];
            auto lo=static_cast<long long>(region.position[a]),hi=lo+region.size[a];
            min[a]=first?lo:std::min(min[a],lo);max[a]=first?hi:std::max(max[a],hi);}
        first=false;
        if(count!=region.indices.size()||region.palette.empty())throw std::runtime_error("Region cell count mismatch");
        volume+=count;
        for(auto index:region.indices){if(index>=region.palette.size())throw std::runtime_error("Invalid palette index");if(region.palette[index].name!="minecraft:air")++blocks;}
    }
    if(volume>INT32_MAX||blocks>INT32_MAX)throw std::runtime_error("Litematic cell count exceeds int32");
    JavaNbtWriter writer;
    writer.compound("",[&]{
        writer.intTag("Version",6);writer.intTag("SubVersion",1);writer.intTag("MinecraftDataVersion",dataVersion);
        writer.compound("Metadata",[&]{writer.stringTag("Name",name);writer.stringTag("Author",author);writer.stringTag("Description",description);
            auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            writer.longTag("TimeCreated",now);writer.longTag("TimeModified",now);writer.intTag("RegionCount",static_cast<int>(regions.size()));
            writer.intTag("TotalVolume",static_cast<int>(volume));writer.intTag("TotalBlocks",static_cast<int>(blocks));
            std::array<int,3> enclosing;for(int a=0;a<3;++a){if(max[a]-min[a]>INT32_MAX)throw std::runtime_error("Enclosing size exceeds int32");enclosing[a]=static_cast<int>(max[a]-min[a]);}writer.xyz("EnclosingSize",enclosing);});
        writer.compound("Regions",[&]{
            for(std::size_t n=0;n<regions.size();++n){auto const& region=regions[n];
                writer.compound(region.name.empty()?"region-"+std::to_string(n+1):region.name,[&]{
                    writer.xyz("Position",region.position);writer.xyz("Size",region.size);
                    JavaNbtTag::List palette;
                    for(auto const& state:region.palette){JavaNbtTag::Compound entry{{"Name",JavaNbtTag{state.name}}};
                        JavaNbtTag::Compound properties;for(auto const& [key,value]:state.properties)properties[key]=JavaNbtTag{value};
                        if(!properties.empty())entry["Properties"]=JavaNbtTag{std::move(properties)};palette.push_back(JavaNbtTag{std::move(entry)});}
                    writer.named("BlockStatePalette",JavaNbtTag{std::move(palette)});
                    unsigned bits=std::max(2u,static_cast<unsigned>(std::bit_width(static_cast<unsigned>(region.palette.size()-1))));
                    std::vector<std::uint64_t> words((region.indices.size()*bits+63)/64);
                    for(std::size_t i=0;i<region.indices.size();++i){auto offset=i*bits;auto value=std::uint64_t(region.indices[i]);
                        words[offset/64]|=value<<(offset%64);if(offset%64+bits>64)words[offset/64+1]|=value>>(64-offset%64);}
                    writer.header(12,"BlockStates");writer.scalar<std::int32_t>(static_cast<int>(words.size()));for(auto word:words)writer.scalar(word);
                    writer.named("TileEntities",JavaNbtTag{JavaNbtTag::List(region.blockEntities.begin(),region.blockEntities.end())});
                    writer.named("Entities",JavaNbtTag{JavaNbtTag::List{}});writer.named("PendingBlockTicks",JavaNbtTag{JavaNbtTag::List{}});
                    writer.named("PendingFluidTicks",JavaNbtTag{JavaNbtTag::List{}});
                });
            }
        });
    });
    return gzipJavaNbt(writer.take());
}
}
