#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace lholo::app {
struct VerificationGroup {
    std::string id,expected,found;
    int kind{};
    std::uint64_t count{};
    bool ignored{};
    std::vector<std::array<int,3>> positions;
};
struct VerifierStatus {
    std::uint64_t revision{},checked{},total{};
    bool running{},complete{},waitingChunks{},showMarkers{true};
    std::string placementId,name,error;
    std::array<std::uint64_t,5> counts{};
    std::uint64_t unlistedMismatches{}; // mismatches counted in `counts` whose group was not listed (group limit)
    std::vector<VerificationGroup> groups;
};
VerifierStatus verifierStatus();
std::shared_ptr<VerifierStatus const> verifierStatusShared();
bool queueVerifierCommand(std::string const&,std::string const& value={});
void processVerifierActions();
void resetVerifierActions();
}
