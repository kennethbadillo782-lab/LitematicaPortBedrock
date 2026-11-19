#pragma once
#include <cstdint>
namespace lholo::structure {
enum class VerificationKind {Correct,Missing,Extra,WrongBlock,WrongState};
struct VerificationBlock {bool air; std::uint64_t state; bool sameType;};
// Primary and secondary fluid states both belong to a schematic cell.
inline VerificationKind compareVerificationCell(VerificationBlock expected, VerificationBlock actual,
    VerificationBlock expectedLiquid, VerificationBlock actualLiquid) {
    if(expected.air&&expectedLiquid.air) return actual.air&&actualLiquid.air?VerificationKind::Correct:VerificationKind::Extra;
    if(actual.air&&actualLiquid.air) return VerificationKind::Missing;
    bool primary=expected.air==actual.air&&(expected.air||expected.state==actual.state);
    bool liquid=expectedLiquid.air==actualLiquid.air&&(expectedLiquid.air||expectedLiquid.state==actualLiquid.state);
    if(primary&&liquid)return VerificationKind::Correct;
    if(expected.air!=actual.air||(!expected.air&&!actual.sameType))return VerificationKind::WrongBlock;
    if(expectedLiquid.air!=actualLiquid.air||(!expectedLiquid.air&&!actualLiquid.sameType))return VerificationKind::WrongBlock;
    return VerificationKind::WrongState;
}
}
