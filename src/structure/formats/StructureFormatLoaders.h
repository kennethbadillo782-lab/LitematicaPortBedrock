// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Structure format loading. The loaders own .mcstructure/.litematic parsing
// and the loaded-structure generation counter; StructureLoader keeps the
// session state, menu and HUD orchestration.

#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace lholo::structure {

struct LoadedStructure;

namespace detail {

std::shared_ptr<LoadedStructure> loadStructureFile(std::filesystem::path const& path, std::string& error);
// Re-resolve an edited source archive against the current Bedrock block registry.
// Call under the same native parser/world-lifetime gate as loadStructureFile.
std::shared_ptr<LoadedStructure> loadLitematicMemory(std::filesystem::path const& path,
    std::shared_ptr<std::string const> archive, std::string& error);

} // namespace lholo::structure::detail

} // namespace lholo::structure
