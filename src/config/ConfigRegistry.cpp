#include "config/ConfigRegistry.h"
#include "config/UpstreamData.h"
#include <zlib.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
namespace lholo::config {
Json const& upstreamCatalog(){
    static Json const catalog=[] {
        std::string plain(upstream::plainSize,'\0');uLongf count=static_cast<uLongf>(plain.size());
        if(uncompress(reinterpret_cast<Bytef*>(plain.data()),&count,upstream::compressed,sizeof(upstream::compressed))!=Z_OK||count!=plain.size())
            throw std::runtime_error("Could not decode upstream Litematica resources");
        return Json::parse(plain);
    }();return catalog;
}
std::string translate(std::string_view locale,std::string_view key){
    std::string normalized(locale);std::transform(normalized.begin(),normalized.end(),normalized.begin(),[](unsigned char c){return c=='-'?'_':static_cast<char>(std::tolower(c));});
    auto const& languages=upstreamCatalog().at("languages");
    if(!languages.contains(normalized)){auto prefix=normalized.substr(0,normalized.find('_'));normalized=prefix=="es"?"es_es":"en_us";}
    for(auto const& code:{normalized,std::string("en_us")}){auto const& language=languages.at(code);auto found=language.find(std::string(key));if(found!=language.end()&&found->is_string())return found->get<std::string>();}
    return std::string(key);
}
namespace {
Json const* definition(std::string const& group,std::string const& name){
    auto const& list=upstreamCatalog().at(group=="Hotkeys"?"hotkeys":"options");
    for(auto const& item:list)if(item.at("name")==name&&(group=="Hotkeys"||item.at("group")==group))return &item;
    return nullptr;
}
Json defaultValue(Json const& item,bool hotkey){
    if(hotkey)return Json{{"keys",item.at("keys")}};
    if(item.at("type")=="ConfigBooleanHotkeyed")return Json{{"enabled",item.at("default")},{"hotkey",{{"keys",item.at("hotkeyDefault")}}}};
    return item.at("default");
}
bool validHotkey(Json const& value){
    if(value.is_string())return value.get_ref<std::string const&>().size()<=1024;
    if(!value.is_object()||!value.contains("keys")||!value.at("keys").is_string()||value.at("keys").get_ref<std::string const&>().size()>1024)return false;
    if(!value.contains("settings"))return true;
    auto const& settings=value.at("settings");if(!settings.is_object())return false;
    for(auto const* field:{"allow_extra_keys","allow_empty","order_sensitive","exclusive","cancel"})if(settings.contains(field)&&!settings.at(field).is_boolean())return false;
    if(settings.contains("activate_on")&&settings.at("activate_on")!="PRESS"&&settings.at("activate_on")!="RELEASE"&&settings.at("activate_on")!="BOTH")return false;
    if(settings.contains("context")&&settings.at("context")!="INGAME"&&settings.at("context")!="GUI"&&settings.at("context")!="ANY")return false;
    return true;
}
bool valid(Json const& item,Json const& value,bool hotkey){
    if(hotkey)return validHotkey(value);
    auto type=item.at("type").get<std::string>();
    if(type=="ConfigBoolean")return value.is_boolean();
    if(type=="ConfigBooleanHotkeyed")return value.is_boolean()||(value.is_object()&&value.contains("enabled")&&value.at("enabled").is_boolean()&&(!value.contains("hotkey")||validHotkey(value.at("hotkey"))));
    if(type=="ConfigInteger"||type=="ConfigFloat"||type=="ConfigDouble"){
        if(!value.is_number()||(type=="ConfigInteger"&&!value.is_number_integer()))return false;
        auto number=value.get<double>();if(!std::isfinite(number))return false;
        if(item.contains("min")&&number<item.at("min").get<double>())return false;
        if(item.contains("max")&&number>item.at("max").get<double>())return false;
        if(item.contains("runtimeMax")&&number>std::max(2u,std::thread::hardware_concurrency()/4))return false;
        return true;
    }
    if(type=="ConfigStringList")return value.is_array()&&value.size()<=4096&&std::all_of(value.begin(),value.end(),[](auto const& s){return s.is_string()&&s.template get_ref<std::string const&>().size()<=32768;});
    if(!value.is_string()||value.get_ref<std::string const&>().size()>32768)return false;
    if(type=="ConfigColor"){
        auto const& text=value.get_ref<std::string const&>();return (text.size()==7||text.size()==9)&&text.front()=='#'&&std::all_of(text.begin()+1,text.end(),[](unsigned char c){return std::isxdigit(c)!=0;});
    }
    if(type=="ConfigOptionList"&&!item.at("choices").empty())return std::find(item.at("choices").begin(),item.at("choices").end(),value)!=item.at("choices").end();
    return true;
}
Json canonical(Json const& item,Json value,bool hotkey){
    if(hotkey&&value.is_string())return Json{{"keys",value}};
    if(!hotkey&&item.at("type")=="ConfigBooleanHotkeyed"&&value.is_boolean()){auto result=defaultValue(item,false);result["enabled"]=value;return result;}
    return value;
}
}
Registry::Registry():values_(Json::object()),original_(Json::object()){
    for(auto const& item:upstreamCatalog().at("options"))values_[item.at("group").get<std::string>()][item.at("name").get<std::string>()]=defaultValue(item,false);
    for(auto const& item:upstreamCatalog().at("hotkeys"))values_["Hotkeys"][item.at("name").get<std::string>()]=defaultValue(item,true);
}
Json Registry::value(std::string const& group,std::string const& name)const{return values_.at(group).at(name);}
bool Registry::set(std::string const& group,std::string const& name,Json const& value){
    auto* item=definition(group,name);if(!item||!valid(*item,value,group=="Hotkeys"))return false;
    values_[group][name]=canonical(*item,value,group=="Hotkeys");return true;
}
bool Registry::reset(std::string const& group,std::string const& name){auto* item=definition(group,name);return item&&set(group,name,defaultValue(*item,group=="Hotkeys"));}
void Registry::load(Json const& root){
    if(!root.is_object())throw std::invalid_argument("Litematica configuration must be an object");
    Registry next;next.original_=root;
    for(auto const* group:{"Generic","Visuals","InfoOverlays","Colors","Hotkeys"}){
        if(!root.contains(group))continue;if(!root.at(group).is_object())throw std::invalid_argument(std::string("Invalid configuration category: ")+group);
        for(auto const& [name,value]:root.at(group).items())if(definition(group,name)&&!next.set(group,name,value))throw std::invalid_argument(std::string(group)+"."+name+": invalid value");
    }
    *this=std::move(next);
}
Json Registry::serialize()const{
    auto result=original_;
    for(auto const& [group,entries]:values_.items())for(auto const& [name,value]:entries.items()){
        auto* item=definition(group,name);
        // Java only saves entries in OPTIONS. Preserve imported extension fields, but do not introduce an internal unlisted declaration.
        if(item&&!item->value("listed",true))continue;
        result[group][name]=value;
    }
    return result;
}
bool Registry::loadFile(std::filesystem::path const& path){if(!std::filesystem::exists(path))return false;std::ifstream in(path,std::ios::binary);in.exceptions(std::ios::badbit);load(Json::parse(in));return true;}
void Registry::saveFile(std::filesystem::path const& path)const{
    std::filesystem::create_directories(path.parent_path());auto temporary=path;temporary+=L".tmp";
    {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out.exceptions(std::ios::failbit|std::ios::badbit);out<<serialize().dump(2)<<'\n';out.close();}
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Could not commit litematica.json");
}
}
