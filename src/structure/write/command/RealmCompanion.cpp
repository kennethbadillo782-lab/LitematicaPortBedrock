#include "structure/write/command/RealmCompanion.h"
#include "plugin/LHolo.h"
#include "ll/api/mod/NativeMod.h"
#include <filesystem>
namespace lholo::structure::write::command {
bool realmCompanionEnabled() {
    std::error_code ec;
    return std::filesystem::exists(LHolo::getInstance().getSelf().getDataDir() / "realm_companion.enabled", ec);
}
}
