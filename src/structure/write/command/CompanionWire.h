#pragma once
#include <string>
#include <vector>
#include <stdexcept>
namespace lholo::structure::write::command {
inline constexpr std::size_t CompanionCommandLimit=480;
// LP3 job fragments use almost the whole 1000-character command limit of CommandRequestPacket.
inline constexpr std::size_t CompanionJobCommandLimit=999;
inline std::vector<std::string> fragmentCompanionBatch(std::string const& hex,std::string const& transfer){
    if(hex.empty()||hex.size()>5200||transfer.empty()||transfer.size()>36)throw std::invalid_argument("Invalid companion transfer");
    std::vector<std::string> commands;
    auto total=(hex.size()+399)/400;
    for(std::size_t part=0;part<total;++part){
        auto text="/litematica:chunk "+transfer+" "+std::to_string(part)+" "+std::to_string(total)+" "+hex.substr(part*400,400);
        if(text.size()>CompanionCommandLimit)throw std::length_error("Companion command too long");
        commands.push_back(std::move(text));
    }
    return commands;
}
inline std::vector<std::string> companionResultQueries(std::vector<std::string> const& tokens){
    std::vector<std::string> commands;std::string text="/litematica:result ";
    constexpr std::size_t prefix=sizeof("/litematica:result ")-1;
    for(auto const& token:tokens){
        if(token.empty()||token.size()>128)throw std::invalid_argument("Invalid result token");
        if(text.size()+token.size()+1>CompanionCommandLimit){commands.push_back(text);text="/litematica:result ";}
        if(text.size()>prefix)text+=',';
        text+=token;
    }
    if(text.size()>prefix)commands.push_back(text);
    return commands;
}
}
