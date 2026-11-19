#include "structure/VerifierComparison.h"

#include <cstdio>

int runVerifierComparisonTests() {
    using namespace lholo::structure;
    int failures{}, checks{};
    auto check = [&](bool value, char const* message) {
        ++checks;
        if (!value) { ++failures; std::fprintf(stderr, "VerifierComparisonTests: %s\n", message); }
    };
    VerificationBlock const air{true, 0, true};
    VerificationBlock const stone{false, 1, true};
    VerificationBlock const stoneOtherState{false, 2, true};   // same block type, different state
    VerificationBlock const dirt{false, 3, false};             // different block type
    VerificationBlock const water{false, 10, true};
    VerificationBlock const waterOtherState{false, 11, true};
    VerificationBlock const lava{false, 12, false};

    // Air in the schematic: anything in the world is extra, empty world is correct.
    check(compareVerificationCell(air, air, air, air) == VerificationKind::Correct, "air vs air");
    check(compareVerificationCell(air, stone, air, air) == VerificationKind::Extra, "air vs block is extra");
    check(compareVerificationCell(air, air, air, water) == VerificationKind::Extra, "air vs liquid only is extra");
    // Block expected, nothing found.
    check(compareVerificationCell(stone, air, air, air) == VerificationKind::Missing, "block vs air is missing");
    check(compareVerificationCell(air, air, water, air) == VerificationKind::Missing, "liquid vs air is missing");
    // Equal cells.
    check(compareVerificationCell(stone, stone, air, air) == VerificationKind::Correct, "same block");
    check(compareVerificationCell(stone, stone, water, water) == VerificationKind::Correct, "same block and liquid");
    check(compareVerificationCell(air, air, water, water) == VerificationKind::Correct, "same liquid only");
    // Different state of the same type, or a different type.
    check(compareVerificationCell(stone, stoneOtherState, air, air) == VerificationKind::WrongState, "state differs");
    check(compareVerificationCell(stone, dirt, air, air) == VerificationKind::WrongBlock, "type differs");
    check(compareVerificationCell(air, air, water, waterOtherState) == VerificationKind::WrongState, "liquid state differs");
    check(compareVerificationCell(air, air, water, lava) == VerificationKind::WrongBlock, "liquid type differs");
    // Primary correct but liquid wrong or absent.
    check(compareVerificationCell(stone, stone, water, air) == VerificationKind::WrongBlock, "liquid missing under correct block");
    check(compareVerificationCell(stone, stone, air, water) == VerificationKind::WrongBlock, "extra liquid under correct block");
    check(compareVerificationCell(stone, stone, water, waterOtherState) == VerificationKind::WrongState, "block ok, liquid state differs");
    // Block expected where the world has only liquid.
    check(compareVerificationCell(stone, air, air, water) == VerificationKind::WrongBlock, "block vs liquid only");
    // Wrong block takes priority over a wrong state of the other layer.
    check(compareVerificationCell(stone, dirt, water, waterOtherState) == VerificationKind::WrongBlock, "type error wins");
    std::printf("VerifierComparisonTests: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
