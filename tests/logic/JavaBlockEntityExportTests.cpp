#include "structure/formats/JavaBlockEntityExport.h"

#include <cstdio>

int runJavaBlockEntityExportTests() {
    using namespace lholo::structure;
    using lholo::structure::detail::JavaNbtTag;
    int failures{}, checks{};
    auto check = [&](bool value, char const* message) {
        ++checks;
        if (!value) { ++failures; std::fprintf(stderr, "JavaBlockEntityExportTests: %s\n", message); }
    };
    auto throws = [&](auto&& body) { try { body(); } catch (std::runtime_error const&) { return true; } return false; };
    auto get = [](JavaNbtTag const& tag, char const* key) -> JavaNbtTag const& {
        return std::get<JavaNbtTag::Compound>(tag.value).at(key);
    };
    auto str = [](JavaNbtTag const& tag) { return std::get<std::string>(tag.value); };

    JavaExportIssues issues;
    check(issues.empty(), "no export issues initially");
    issues.record("block entity Barrel needs item-data adapter");
    issues.record("block entity Barrel needs item-data adapter");
    issues.record("block state minecraft:example_block");
    auto issueSummary = issues.summary();
    check(issueSummary.find("3 datos incompatibles en 2 tipos") != std::string::npos
        && issueSummary.find("block entity Barrel needs item-data adapter (2)") != std::string::npos
        && issueSummary.find("block state minecraft:example_block (1)") != std::string::npos,
        "export issues summarize every unsupported type and count");
    auto warning = issues.warning();
    check(warning.rfind("Aviso: se guardó el esquema", 0) == 0 && warning.find("3 datos") != std::string::npos
        && warning.find("Exportación cancelada") == std::string::npos, "export warning does not cancel the save");

    for (char const* simple : {"minecraft:bed", "minecraft:beehive", "minecraft:lectern"}) {
        auto entity = exportJavaSimpleBlockEntity(simple, {1, 2, 3});
        auto const& fields = std::get<JavaNbtTag::Compound>(entity.value);
        check(std::get<std::string>(fields.at("id").value) == simple && std::get<std::int32_t>(fields.at("z").value) == 3,
            "simple block entities export id and position");
    }
    auto stand = exportJavaContainer("minecraft:brewing_stand", {}, 5, {0, 0, 0});
    check(std::get<JavaNbtTag::List>(std::get<JavaNbtTag::Compound>(stand.value).at("Items").value).empty(), "brewing stand exports");

    BedrockSign sign;
    sign.front.text = "Hello\nworld";
    sign.back.text = "";
    sign.waxed = true;
    sign.front.glowing = true;
    auto modern = exportJavaSign(sign, {3, 4, 5}, 4400);
    check(str(get(modern, "id")) == "minecraft:sign", "sign id");
    check(std::get<std::int8_t>(get(modern, "is_waxed").value) == 1, "waxed");
    check(std::get<std::int32_t>(get(modern, "x").value) == 3 && std::get<std::int32_t>(get(modern, "z").value) == 5, "relative position");
    auto const& front = get(modern, "front_text");
    auto const& messages = std::get<JavaNbtTag::List>(get(front, "messages").value);
    check(messages.size() == 4, "four messages");
    check(str(messages[0]) == "Hello" && str(messages[1]) == "world" && str(messages[2]).empty(), "plain text components from 1.21.5");
    check(str(get(front, "color")) == "black", "black dye");
    check(std::get<std::int8_t>(get(front, "has_glowing_text").value) == 1, "glowing text");
    check(std::get<std::int8_t>(get(get(modern, "back_text"), "has_glowing_text").value) == 0, "back not glowing");

    // Before 1.21.5 the components are JSON strings.
    auto legacy = exportJavaSign(sign, {0, 0, 0}, 4000);
    auto const& legacyMessages = std::get<JavaNbtTag::List>(get(get(legacy, "front_text"), "messages").value);
    check(str(legacyMessages[0]) == "\"Hello\"" && str(legacyMessages[3]) == "\"\"", "JSON string components before 1.21.5");
    BedrockSign quoted;
    quoted.front.text = "say \"hi\"";
    auto quotedTag = exportJavaSign(quoted, {0, 0, 0}, 4000);
    check(str(std::get<JavaNbtTag::List>(get(get(quotedTag, "front_text"), "messages").value)[0]) == "\"say \\\"hi\\\"\"", "quotes escaped");

    sign.hanging = true;
    check(str(get(exportJavaSign(sign, {0, 0, 0}, 4400), "id")) == "minecraft:hanging_sign", "hanging sign id");

    auto barrel = exportJavaBarrel({{"minecraft:oak_planks", 32, 4}}, {8, 9, 10}, "Storage", "key");
    check(str(get(barrel, "id")) == "minecraft:barrel", "barrel id");
    check(std::get<std::int32_t>(get(barrel, "x").value) == 8
        && std::get<std::int32_t>(get(barrel, "y").value) == 9
        && std::get<std::int32_t>(get(barrel, "z").value) == 10, "barrel local coordinates");
    auto const& items = std::get<JavaNbtTag::List>(get(barrel, "Items").value);
    check(items.size() == 1 && str(get(items.front(), "id")) == "minecraft:oak_planks"
        && std::get<std::int32_t>(get(items.front(), "count").value) == 32
        && std::get<std::int8_t>(get(items.front(), "Slot").value) == 4, "barrel item stack");
    auto const& modernName = std::get<JavaNbtTag::Compound>(get(barrel, "CustomName").value);
    check(modernName.contains("text") && str(modernName.at("text")) == "Storage" && str(get(barrel, "Lock")) == "key", "modern barrel component name and lock");
    auto legacyBarrel = exportJavaBarrel({}, {0, 0, 0}, "Storage", {}, 3000);
    check(str(get(legacyBarrel, "CustomName")) == "{\"text\":\"Storage\"}", "legacy barrel component name");
    check(std::get<JavaNbtTag::List>(get(exportJavaBarrel({}, {0, 0, 0}), "Items").value).empty(), "empty barrel inventory");
    check(throws([&] { exportJavaBarrel({{"mod:item", 1, 0}}, {0, 0, 0}); }), "non-Minecraft item names refused");
    check(throws([&] { exportJavaBarrel({{"minecraft:stone", 1, 27}}, {0, 0, 0}); }), "out-of-range slot refused");
    check(throws([&] { exportJavaBarrel({{"minecraft:stone", 1, 0}, {"minecraft:dirt", 1, 0}}, {0, 0, 0}); }), "duplicate slots refused");

    // Anything that cannot be reproduced exactly must stop the export.
    BedrockSign formatted;
    formatted.front.text = "\xC2\xA7" "cred";
    check(throws([&] { exportJavaSign(formatted, {0, 0, 0}, 4400); }), "formatting codes refused");
    BedrockSign coloured;
    coloured.back.color = static_cast<std::int32_t>(0xffff0000u);
    check(throws([&] { exportJavaSign(coloured, {0, 0, 0}, 4400); }), "unverified colour refused");
    BedrockSign tall;
    tall.front.text = "1\n2\n3\n4\n5";
    check(throws([&] { exportJavaSign(tall, {0, 0, 0}, 4400); }), "more than four lines refused");
    std::printf("JavaBlockEntityExportTests: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
