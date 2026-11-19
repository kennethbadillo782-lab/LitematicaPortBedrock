// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <optional>
#include <utility>
#include <vector>

class Block;

namespace lholo::structure {

struct JavaBlockState {
    std::string name;
    std::vector<std::pair<std::string,std::string>> properties;
};
std::optional<JavaBlockState> exportJavaBlockState(Block const&, bool waterlogged=false);
// Best-effort Java state when no exact mapping exists (same-name state, or a direct name match). Never throws.
// `neighbor(dx,dy,dz)` returns the block next to this one (or null); it lets connections and stair shapes be computed as Java does.
// `computed` is set when the result follows Java's own neighbour rules (not an approximation).
std::optional<JavaBlockState> exportJavaBlockStateApproximate(
    Block const&, bool waterlogged, std::function<Block const*(int,int,int)> const& neighbor, bool* computed);
int javaExportDataVersion();
// Walks every block permutation registered in the running game and writes which ones have no verified Java equivalent.
void writeJavaExportAudit(std::filesystem::path const& file);
struct ResolvedJavaBlock {
    Block const* block{};
    Block const* liquid{};
    bool         mapped{};
};

// Resolve one exact Java block state through the table generated from Chunker.
// javaDataVersion is the MinecraftDataVersion stored in the litematic.
ResolvedJavaBlock resolveJavaBlockState(
    std::string const&                                      javaName,
    std::vector<std::pair<std::string, std::string>> const& properties,
    int                                                     javaDataVersion
);

// Minecraft owns the Block pointers. Clear them at every world lifetime
// boundary; the generated string mapping itself is process-lifetime data.
void resetJavaBlockMappingCache();

} // namespace lholo::structure
