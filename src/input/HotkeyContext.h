#pragma once
#include <string_view>

namespace lholo::input {
// Bare keys must never be claimed by chat, text editors, inventory or dialogs.
constexpr bool acceptsProjectionHotkeys(bool playing, bool idle, std::string_view screen) {
    return (playing && screen == "hud_screen") || (idle && screen == "start_screen");
}
}
