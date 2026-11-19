#pragma once
// Bedrock block entity -> Java tile entity adapters for .litematic export.
// An adapter either reproduces the data exactly or throws naming what cannot be represented; it never drops fields.
#include "structure/formats/LitematicNbt.h"
#include <array>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace lholo::structure {

using detail::JavaNbtTag;
namespace detail { inline constexpr int kTextComponentNbtDataVersion = 4325; }

struct BedrockSignSide {
    std::string text;                                  // lines joined with '\n', as Bedrock stores them
    std::int32_t color{static_cast<std::int32_t>(0xff000000u)};
    bool glowing{};
};
struct BedrockSign {
    BedrockSignSide front, back;
    bool waxed{};
    bool hanging{};
};

struct JavaContainerItem {
    std::string id;
    std::int32_t count{};
    std::int8_t slot{};
};

inline JavaNbtTag exportJavaSimpleBlockEntity(
    std::string const& javaId, std::array<int, 3> position, std::string const& customName = {},
    int dataVersion = detail::kTextComponentNbtDataVersion
) {
    if (javaId != "minecraft:bell" && javaId != "minecraft:ender_chest"
        && javaId != "minecraft:enchanting_table" && javaId != "minecraft:bed" && javaId != "minecraft:beehive"
        && javaId != "minecraft:lectern")
        throw std::runtime_error("Java export does not recognize this simple block entity");
    JavaNbtTag::Compound result;
    result["id"] = JavaNbtTag{javaId};
    result["x"] = JavaNbtTag{static_cast<std::int32_t>(position[0])};
    result["y"] = JavaNbtTag{static_cast<std::int32_t>(position[1])};
    result["z"] = JavaNbtTag{static_cast<std::int32_t>(position[2])};
    if (!customName.empty()) {
        if (dataVersion >= detail::kTextComponentNbtDataVersion)
            result["CustomName"] = JavaNbtTag{JavaNbtTag::Compound{{"text", JavaNbtTag{customName}}}};
        else
            result["CustomName"] = JavaNbtTag{nlohmann::json{{"text", customName}}.dump()};
    }
    return JavaNbtTag{std::move(result)};
}

class JavaExportIssues {
    std::map<std::string, std::uint64_t> mCounts;
public:
    void record(std::string description) { ++mCounts[std::move(description)]; }
    bool empty() const { return mCounts.empty(); }
    std::string warning() const {
        std::ostringstream out;
        std::uint64_t total{};
        for (auto const& [_, count] : mCounts) total += count;
        out << "Aviso: se guardó el esquema, pero " << total << " datos se aproximaron u omitieron en " << mCounts.size() << " tipos: ";
        bool first = true;
        for (auto const& [description, count] : mCounts) {
            if (!first) out << "; ";
            first = false;
            out << description << " (" << count << ')';
        }
        return out.str();
    }
    std::string summary() const {
        std::ostringstream out;
        std::uint64_t total{};
        for (auto const& [_, count] : mCounts) total += count;
        out << "Exportación cancelada: " << total << " datos incompatibles en " << mCounts.size()
            << " tipos. Corrige todos los indicados y vuelve a guardar: ";
        bool first = true;
        for (auto const& [description, count] : mCounts) {
            if (!first) out << "; ";
            first = false;
            out << description << " (" << count << ')';
        }
        return out.str();
    }
};

namespace detail {
// 1.21.5 (data version 4325) stores text components as NBT: a plain text component is the plain string itself.
// Earlier versions store the component as a JSON string.
inline std::string javaSignMessage(std::string const& line, int dataVersion) {
    if (dataVersion >= kTextComponentNbtDataVersion) return line;
    return nlohmann::json(line).dump();
}

inline JavaNbtTag javaSignSide(BedrockSignSide const& side, int dataVersion) {
    if (side.text.find("\xC2\xA7") != std::string::npos)
        throw std::runtime_error("Java export cannot represent sign formatting codes exactly");
    // Only the default dye is verified against a Bedrock export; any other colour needs its own mapping sample.
    if (side.color != static_cast<std::int32_t>(0xff000000u))
        throw std::runtime_error("Java export has no verified mapping for this sign text colour");
    std::vector<std::string> lines{std::string{}};
    for (char c : side.text) {
        if (c == '\n') lines.emplace_back();
        else lines.back().push_back(c);
    }
    if (lines.size() > 4) throw std::runtime_error("Sign text has more than four lines");
    lines.resize(4);
    JavaNbtTag::List messages;
    for (auto const& line : lines) messages.push_back(JavaNbtTag{javaSignMessage(line, dataVersion)});
    JavaNbtTag::Compound result;
    result["messages"] = JavaNbtTag{std::move(messages)};
    result["color"] = JavaNbtTag{std::string{"black"}};
    result["has_glowing_text"] = JavaNbtTag{static_cast<std::int8_t>(side.glowing ? 1 : 0)};
    return JavaNbtTag{std::move(result)};
}
} // namespace detail

// `position` is relative to the region origin, as the litematic format stores tile entities.
inline JavaNbtTag exportJavaSign(BedrockSign const& sign, std::array<int, 3> position, int dataVersion) {
    JavaNbtTag::Compound result;
    result["id"] = JavaNbtTag{std::string{sign.hanging ? "minecraft:hanging_sign" : "minecraft:sign"}};
    result["is_waxed"] = JavaNbtTag{static_cast<std::int8_t>(sign.waxed ? 1 : 0)};
    result["front_text"] = detail::javaSignSide(sign.front, dataVersion);
    result["back_text"] = detail::javaSignSide(sign.back, dataVersion);
    result["x"] = JavaNbtTag{static_cast<std::int32_t>(position[0])};
    result["y"] = JavaNbtTag{static_cast<std::int32_t>(position[1])};
    result["z"] = JavaNbtTag{static_cast<std::int32_t>(position[2])};
    return JavaNbtTag{std::move(result)};
}

inline JavaNbtTag exportJavaBarrel(
    std::vector<JavaContainerItem> const& items,
    std::array<int, 3> position,
    std::string const& customName = {},
    std::string const& lock = {},
    int dataVersion = detail::kTextComponentNbtDataVersion
) {
    JavaNbtTag::Compound result;
    result["id"] = JavaNbtTag{std::string{"minecraft:barrel"}};
    result["x"] = JavaNbtTag{static_cast<std::int32_t>(position[0])};
    result["y"] = JavaNbtTag{static_cast<std::int32_t>(position[1])};
    result["z"] = JavaNbtTag{static_cast<std::int32_t>(position[2])};
    JavaNbtTag::List javaItems;
    javaItems.reserve(items.size());
    std::array<bool, 27> occupied{};
    for (auto const& item : items) {
        auto const slot = static_cast<unsigned char>(item.slot);
        if (item.id.rfind("minecraft:", 0) != 0 || item.count <= 0 || item.count > 127 || slot >= occupied.size())
            throw std::runtime_error("Java export found an invalid Bedrock barrel item");
        if (occupied[slot]) throw std::runtime_error("Java export found duplicate barrel inventory slots");
        occupied[slot] = true;
        JavaNbtTag::Compound entry;
        entry["id"] = JavaNbtTag{item.id};
        entry["count"] = JavaNbtTag{item.count};
        entry["Slot"] = JavaNbtTag{item.slot};
        javaItems.emplace_back(std::move(entry));
    }
    result["Items"] = JavaNbtTag{std::move(javaItems)};
    if (!customName.empty()) {
        if (dataVersion >= detail::kTextComponentNbtDataVersion)
            result["CustomName"] = JavaNbtTag{JavaNbtTag::Compound{{"text", JavaNbtTag{customName}}}};
        else
            result["CustomName"] = JavaNbtTag{nlohmann::json{{"text", customName}}.dump()};
    }
    if (!lock.empty()) result["Lock"] = JavaNbtTag{lock};
    return JavaNbtTag{std::move(result)};
}

inline JavaNbtTag exportJavaContainer(
    std::string const& javaId,
    std::vector<JavaContainerItem> const& items,
    std::size_t slotCount,
    std::array<int, 3> position,
    std::string const& customName = {},
    std::string const& lock = {},
    std::vector<std::pair<std::string, std::int16_t>> const& timers = {},
    std::int32_t lastInteractedSlot = -1,
    int dataVersion = detail::kTextComponentNbtDataVersion
) {
    if (javaId != "minecraft:chest" && javaId != "minecraft:furnace"
        && javaId != "minecraft:blast_furnace" && javaId != "minecraft:smoker"
        && javaId != "minecraft:hopper" && javaId != "minecraft:dispenser"
        && javaId != "minecraft:dropper" && javaId != "minecraft:barrel"
        && javaId != "minecraft:chiseled_bookshelf" && javaId != "minecraft:brewing_stand")
        throw std::runtime_error("Java export does not recognize this container type");
    if (slotCount == 0 || slotCount > 256) throw std::runtime_error("Java export found an invalid container size");
    JavaNbtTag::Compound result;
    result["id"] = JavaNbtTag{javaId};
    result["x"] = JavaNbtTag{static_cast<std::int32_t>(position[0])};
    result["y"] = JavaNbtTag{static_cast<std::int32_t>(position[1])};
    result["z"] = JavaNbtTag{static_cast<std::int32_t>(position[2])};
    JavaNbtTag::List javaItems;
    javaItems.reserve(items.size());
    std::vector<bool> occupied(slotCount);
    for (auto const& item : items) {
        auto const slot = static_cast<unsigned char>(item.slot);
        if (item.id.rfind("minecraft:", 0) != 0 || item.count <= 0 || item.count > 127 || slot >= slotCount)
            throw std::runtime_error("Java export found an invalid item in " + javaId);
        if (occupied[slot]) throw std::runtime_error("Java export found duplicate inventory slots in " + javaId);
        occupied[slot] = true;
        JavaNbtTag::Compound entry;
        entry["id"] = JavaNbtTag{item.id};
        entry["count"] = JavaNbtTag{item.count};
        entry["Slot"] = JavaNbtTag{item.slot};
        javaItems.emplace_back(std::move(entry));
    }
    result["Items"] = JavaNbtTag{std::move(javaItems)};
    if (javaId == "minecraft:chiseled_bookshelf") result["last_interacted_slot"] = JavaNbtTag{lastInteractedSlot};
    if (!customName.empty()) {
        if (dataVersion >= detail::kTextComponentNbtDataVersion)
            result["CustomName"] = JavaNbtTag{JavaNbtTag::Compound{{"text", JavaNbtTag{customName}}}};
        else
            result["CustomName"] = JavaNbtTag{nlohmann::json{{"text", customName}}.dump()};
    }
    if (!lock.empty()) result["Lock"] = JavaNbtTag{lock};
    for (auto const& [name, value] : timers) result[name] = JavaNbtTag{value};
    return JavaNbtTag{std::move(result)};
}

} // namespace lholo::structure
