#include "structure/ProjectionRaycast.h"
#include "structure/SchematicEditTarget.h"

#include <cstdint>
#include <array>
#include <cstdio>
#include <memory>

int runProjectionRaycastTests() {
    using namespace lholo::structure;
    int failures{};
    auto check = [&](bool result, char const* name) {
        if (!result) { ++failures; std::printf("ProjectionRaycastTests FAIL: %s\n", name); }
    };
    auto source = std::make_shared<LoadedStructure>();
    source->sizeX = source->sizeY = source->sizeZ = 1;
    source->renderBlocks.push_back({0, 0, 0,
        reinterpret_cast<Block const*>(std::uintptr_t{1})});
    CompositionFile first{"first", source, true, 10, 0, 0};
    CompositionFile second{"second", source, true, 10, 0, 0};
    std::array files{first, second};
    TaskProjectionView view{files};
    auto hit = traceTaskProjection(view, {8.5, 0.5, 0.5}, {1, 0, 0}, 10);
    check(hit && hit->owner.file == 0 && hit->worldCell == std::array<long long,3>{10,0,0}
        && hit->enteredFace == std::array<int,3>{-1,0,0} && hit->distance == 1.5,
        "nearest visible source wins and hit face is preserved");
    check(!traceTaskProjection(view, {8.5, 0.5, 0.5}, {1, 0, 0}, 1.4),
        "range ends before the projected block");
    first.visible = false;
    files[0] = first;
    TaskProjectionView onlySecond{files};
    hit = traceTaskProjection(onlySecond, {8.5, 0.5, 0.5}, {1, 0, 0}, 10);
    check(hit && hit->owner.file == 1, "hidden file does not intercept the ray");
    hit = traceTaskProjection(onlySecond, {12.5, 0.5, 0.5}, {-1, 0, 0}, 10);
    check(hit && hit->enteredFace == std::array<int,3>{1,0,0} && hit->distance == 1.5,
        "negative direction enters the opposite face");
    check(!traceTaskProjection(onlySecond, {8.5, 0.5, 0.5}, {0, 0, 0}, 10),
        "zero direction is rejected");
    source->sourceArchive = std::make_shared<std::string const>("source");
    source->regions.push_back({0, 0, 0, 1, 1, 1, -1, "Main"});
    source->renderBlocks.mutableValues()[0].regionIndex = 0;
    hit = traceTaskProjection(onlySecond, {8.5, 0.5, 0.5}, {1, 0, 0}, 10);
    auto target = hit ? schematicEditCell(onlySecond, *hit) : std::nullopt;
    check(target && target->file == 1 && target->regionName == "Main" &&
          target->regionCell == std::array<int,3>{0,0,0},
        "ray hit resolves exact source region and local cell");
    check(hit && !schematicEditCell(onlySecond, *hit, {1, 0, 0}),
        "adjacent edit outside source region is rejected");
    source->regions.push_back({0, 0, 0, 1, 1, 1, -1, "Overlay"});
    source->renderBlocks.mutableValues()[0].regionIndex = 1;
    target = hit ? schematicEditCell(onlySecond, *hit) : std::nullopt;
    check(target && target->regionName == "Overlay",
        "overlapping subregions edit the visible palette winner");
    std::printf("ProjectionRaycastTests: %d failures\n", failures);
    return failures;
}
