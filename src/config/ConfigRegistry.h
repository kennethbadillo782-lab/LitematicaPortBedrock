#pragma once
// Typed configuration port from Configs.java/Hotkeys.java and MaLiLib Config* (LGPL-3.0-or-later).
#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>
#include <string_view>
namespace lholo::config {
using Json=nlohmann::json;
Json const& upstreamCatalog();
std::string translate(std::string_view locale,std::string_view key);
class Registry {
    Json values_,original_;
public:
    Registry();
    Json const& values() const {return values_;}
    Json value(std::string const& group,std::string const& name) const;
    bool set(std::string const& group,std::string const& name,Json const& value);
    bool reset(std::string const& group,std::string const& name);
    // Reject malformed known values transactionally, retain unknown extension fields on save.
    void load(Json const& root);
    Json serialize() const;
    bool loadFile(std::filesystem::path const& path);
    void saveFile(std::filesystem::path const& path) const;
};
}
