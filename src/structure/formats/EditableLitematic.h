#pragma once

#include "structure/formats/JavaNbtWriter.h"
#include "structure/formats/LitematicInput.h"
#include "structure/java_to_bedrock/JavaToBedrock.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace lholo::structure {

// Owns the complete Java source document. Projection loading deliberately drops
// ordinary entities; editing must retain those tags and all unknown metadata.
class EditableLitematic {
public:
    explicit EditableLitematic(std::string_view archive) {
        std::string error;
        auto inflated = detail::inflateGzip(archive, error);
        if (!inflated) throw std::runtime_error(error);
        mInflated = std::move(*inflated);
        mRoot = detail::JavaNbtReader{mInflated, true}.readRoot();
        if (!compound(mRoot, "Regions") || !compound(mRoot, "Metadata"))
            throw std::runtime_error("Litematic is missing Regions or Metadata");
    }
    EditableLitematic(EditableLitematic const&) = delete;
    EditableLitematic& operator=(EditableLitematic const&) = delete;
    EditableLitematic(EditableLitematic&&) = delete;
    EditableLitematic& operator=(EditableLitematic&&) = delete;

    [[nodiscard]] bool dirty() const { return mDirty; }

    // Coordinates are local to the named source region, as in Java's
    // LitematicaBlockStateContainer. World transforms belong to the caller.
    bool setBlock(std::string const& regionName, int x, int y, int z,
                  JavaBlockState const& replacement) {
        auto* regions = compound(mRoot, "Regions");
        auto foundRegion = regions->find(regionName);
        if (foundRegion == regions->end()) throw std::out_of_range("Unknown schematic region");
        auto* region = std::get_if<detail::JavaNbtTag::Compound>(&foundRegion->second.value);
        if (!region) throw std::runtime_error("Malformed schematic region");
        auto const size = regionSize(*region);
        if (x < 0 || y < 0 || z < 0 || x >= size[0] || y >= size[1] || z >= size[2])
            throw std::out_of_range("Schematic block is outside its region");
        auto* palette = list(*region, "BlockStatePalette");
        auto* states = longArray(*region, "BlockStates");
        if (!palette || palette->empty() || !states) throw std::runtime_error("Malformed schematic block data");
        auto const count = static_cast<std::uint64_t>(size[0]) * size[1] * size[2];
        auto const cell = (static_cast<std::uint64_t>(y) * size[2] + z) * size[0] + x;
        auto const oldBits = bitsFor(palette->size());
        auto const oldIndex = detail::packedPaletteIndex(*states, cell, oldBits);
        if (oldIndex >= palette->size()) throw std::runtime_error("Invalid schematic palette index");
        auto const requested = stateTag(replacement);
        std::size_t replacementIndex = 0;
        for (; replacementIndex < palette->size(); ++replacementIndex)
            if (sameState((*palette)[replacementIndex], requested)) break;
        if (replacementIndex == oldIndex) return false;
        bool const appendPalette = replacementIndex == palette->size();
        auto const newBits = bitsFor(palette->size() + (appendPalette ? 1 : 0));
        if (count > detail::kMaximumJavaNbtSize / newBits * 8)
            throw std::length_error("Edited schematic exceeds NBT size limit");
        bool const oldAir = isAir((*palette)[oldIndex]);
        bool const newAir = isAir(requested);
        auto* metadata = compound(mRoot, "Metadata");
        auto total = metadata->find("TotalBlocks");
        auto* totalValue = total == metadata->end() ? nullptr
            : std::get_if<std::int32_t>(&total->second.value);
        if (oldAir != newAir && totalValue &&
            ((newAir && *totalValue <= 0) || (!newAir && *totalValue == INT_MAX)))
            throw std::runtime_error("Schematic block count would overflow");
        bool const removeEntity = !sameBlock((*palette)[oldIndex], requested);
        detail::JavaNbtTag::LongArray repacked;
        if (newBits != oldBits) {
            std::vector<std::uint64_t> words(static_cast<std::size_t>((count * newBits + 63) / 64));
            for (std::uint64_t i = 0; i < count; ++i) {
                auto const index = i == cell ? static_cast<std::uint32_t>(replacementIndex)
                    : detail::packedPaletteIndex(*states, i, oldBits);
                if (index >= palette->size() + (appendPalette ? 1 : 0))
                    throw std::runtime_error("Invalid schematic palette index");
                auto const bit = i * newBits;
                words[static_cast<std::size_t>(bit / 64)] |= std::uint64_t(index) << (bit % 64);
                if (bit % 64 + newBits > 64)
                    words[static_cast<std::size_t>(bit / 64 + 1)] |= std::uint64_t(index) >> (64 - bit % 64);
            }
            std::string packed;
            packed.reserve(words.size() * 8);
            for (auto word : words)
                for (int shift = 56; shift >= 0; shift -= 8)
                    packed.push_back(static_cast<char>(word >> shift));
            repacked = detail::JavaNbtTag::LongArray::owned(std::move(packed));
        } else if (!states->owner) {
            repacked = detail::JavaNbtTag::LongArray::owned(std::string(states->bytes));
        }
        if (appendPalette) palette->push_back(requested);
        if (newBits != oldBits || !states->owner) *states = std::move(repacked);
        if (newBits == oldBits) setPackedIndex(*states, cell, newBits,
                                               static_cast<std::uint32_t>(replacementIndex));
        if (oldAir != newAir && totalValue) *totalValue += newAir ? -1 : 1;
        // A block entity cannot remain attached when its block type changes.
        if (removeEntity) removeBlockEntity(*region, x, y, z);
        auto const now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        (*metadata)["TimeModified"] = detail::JavaNbtTag{static_cast<std::int64_t>(now)};
        mDirty = true;
        return true;
    }

    [[nodiscard]] std::string save() const {
        detail::JavaNbtWriter writer;
        writer.named("", detail::JavaNbtTag{mRoot});
        return detail::gzipJavaNbt(writer.take());
    }

private:
    using Compound = detail::JavaNbtTag::Compound;
    using List = detail::JavaNbtTag::List;
    static Compound* compound(Compound& parent, char const* key) {
        auto found = parent.find(key);
        return found == parent.end() ? nullptr : std::get_if<Compound>(&found->second.value);
    }
    static List* list(Compound& parent, char const* key) {
        auto found = parent.find(key);
        return found == parent.end() ? nullptr : std::get_if<List>(&found->second.value);
    }
    static detail::JavaNbtTag::LongArray* longArray(Compound& parent, char const* key) {
        auto found = parent.find(key);
        return found == parent.end() ? nullptr : std::get_if<detail::JavaNbtTag::LongArray>(&found->second.value);
    }
    static std::array<int, 3> regionSize(Compound const& region) {
        auto found = region.find("Size");
        auto const* size = found == region.end() ? nullptr : std::get_if<Compound>(&found->second.value);
        if (!size) throw std::runtime_error("Schematic region has no size");
        std::array<int, 3> result{};
        for (int axis = 0; axis < 3; ++axis) {
            auto value = size->find(std::string(1, "xyz"[axis]));
            auto const* signedSize = value == size->end() ? nullptr : std::get_if<std::int32_t>(&value->second.value);
            if (!signedSize || *signedSize == 0 || *signedSize == INT_MIN)
                throw std::runtime_error("Invalid schematic region size");
            result[axis] = std::abs(*signedSize);
        }
        return result;
    }
    static unsigned bitsFor(std::size_t paletteSize) {
        if (!paletteSize || paletteSize > UINT32_MAX) throw std::length_error("Invalid schematic palette size");
        return std::max(2u, static_cast<unsigned>(std::bit_width(paletteSize - 1)));
    }
    static void setPackedIndex(detail::JavaNbtTag::LongArray& states, std::uint64_t cell,
                               unsigned bits, std::uint32_t index) {
        auto const bit = cell * bits;
        auto const wordIndex = static_cast<std::size_t>(bit / 64);
        auto const shift = static_cast<unsigned>(bit % 64);
        auto const value = std::uint64_t(index);
        if (shift + bits <= 64) {
            auto const mask = ((std::uint64_t{1} << bits) - 1) << shift;
            states.setWord(wordIndex, (states[wordIndex] & ~mask) | (value << shift));
        } else {
            auto const lowBits = 64 - shift;
            auto const lowMask = (std::uint64_t{1} << lowBits) - 1;
            states.setWord(wordIndex, (states[wordIndex] & ~(lowMask << shift)) |
                                      ((value & lowMask) << shift));
            auto const highMask = (std::uint64_t{1} << (bits - lowBits)) - 1;
            states.setWord(wordIndex + 1, (states[wordIndex + 1] & ~highMask) |
                                          (value >> lowBits));
        }
    }
    static detail::JavaNbtTag stateTag(JavaBlockState const& state) {
        Compound tag{{"Name", detail::JavaNbtTag{state.name}}};
        Compound properties;
        for (auto const& [name, value] : state.properties)
            properties.insert_or_assign(name, detail::JavaNbtTag{value});
        if (!properties.empty()) tag.insert_or_assign("Properties", detail::JavaNbtTag{std::move(properties)});
        return detail::JavaNbtTag{std::move(tag)};
    }
    static bool sameState(detail::JavaNbtTag const& a, detail::JavaNbtTag const& b) {
        auto const* left = std::get_if<Compound>(&a.value);
        auto const* right = std::get_if<Compound>(&b.value);
        if (!left || !right) return false;
        auto leftName = left->find("Name"), rightName = right->find("Name");
        if (leftName == left->end() || rightName == right->end() ||
            std::get_if<std::string>(&leftName->second.value) == nullptr ||
            std::get_if<std::string>(&rightName->second.value) == nullptr ||
            *std::get_if<std::string>(&leftName->second.value) != *std::get_if<std::string>(&rightName->second.value))
            return false;
        auto leftProperties = left->find("Properties"), rightProperties = right->find("Properties");
        if (leftProperties == left->end() || rightProperties == right->end())
            return leftProperties == left->end() && rightProperties == right->end();
        auto const* lp = std::get_if<Compound>(&leftProperties->second.value);
        auto const* rp = std::get_if<Compound>(&rightProperties->second.value);
        if (!lp || !rp || lp->size() != rp->size()) return false;
        for (auto const& [key, value] : *lp) {
            auto found = rp->find(key);
            if (found == rp->end()) return false;
            auto const* l = std::get_if<std::string>(&value.value);
            auto const* r = std::get_if<std::string>(&found->second.value);
            if (!l || !r || *l != *r) return false;
        }
        return true;
    }
    static bool sameBlock(detail::JavaNbtTag const& a, detail::JavaNbtTag const& b) {
        auto const* left = std::get_if<Compound>(&a.value);
        auto const* right = std::get_if<Compound>(&b.value);
        if (!left || !right) return false;
        auto l = left->find("Name"), r = right->find("Name");
        if (l == left->end() || r == right->end()) return false;
        auto const* ln = std::get_if<std::string>(&l->second.value);
        auto const* rn = std::get_if<std::string>(&r->second.value);
        return ln && rn && *ln == *rn;
    }
    static bool isAir(detail::JavaNbtTag const& state) {
        auto const* compound = std::get_if<Compound>(&state.value);
        if (!compound) return false;
        auto found = compound->find("Name");
        auto const* name = found == compound->end() ? nullptr : std::get_if<std::string>(&found->second.value);
        return name && (*name == "minecraft:air" || *name == "minecraft:cave_air" ||
                        *name == "minecraft:void_air");
    }
    static void removeBlockEntity(Compound& region, int x, int y, int z) {
        auto* entities = list(region, "TileEntities");
        if (!entities) return;
        std::erase_if(*entities, [&](detail::JavaNbtTag const& entity) {
            auto const* tag = std::get_if<Compound>(&entity.value);
            if (!tag) return false;
            for (auto const& [key, expected] : {std::pair{"x", x}, {"y", y}, {"z", z}}) {
                auto found = tag->find(key);
                auto const* actual = found == tag->end() ? nullptr : std::get_if<std::int32_t>(&found->second.value);
                if (!actual || *actual != expected) return false;
            }
            return true;
        });
    }
    std::string mInflated;
    Compound mRoot;
    bool mDirty{};
};

} // namespace lholo::structure
