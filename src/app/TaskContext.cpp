#include "app/TaskContext.h"
#include "structure/StructureFileLoader.h"
#include "plugin/LHolo.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/ClientInstanceState.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/BlockPos.h"

namespace lholo::app::detail {
std::uint64_t taskWorldRevision() { return structure::detail::structureFileWorldRevision(); }
std::filesystem::path taskStorePath() {
    auto const directory = LHolo::getInstance().getSelf().getDataDir();
    auto const target = directory / "placements.json";
    auto const legacy = directory / "tasks.json";
    if (!std::filesystem::exists(target) && std::filesystem::exists(legacy)) {
        // The legacy file remains an untouched backup. The v3 writer commits
        // the migrated list atomically only after successful parsing.
        std::filesystem::copy_file(legacy, target, std::filesystem::copy_options::none);
    }
    return target;
}
std::optional<structure::detail::StructureLoadOrigin> taskPlayerOrigin() {
    auto client = ll::service::getClientInstance();
    auto* player = client && client->$getClientInstanceState() == ClientInstanceState::Playing
        ? client->getLocalPlayer() : nullptr;
    if (!player) return {};
    auto pos = player->getFeetBlockPos();
    return structure::detail::StructureLoadOrigin{pos.x,pos.y,pos.z,
        static_cast<int>(player->getDimensionId())};
}
}
