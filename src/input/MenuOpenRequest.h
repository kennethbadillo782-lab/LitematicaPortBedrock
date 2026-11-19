#pragma once
#include <cstdint>
#include <optional>

namespace lholo::input {
// Where the Litematica-style hotkeys (M, M+P, M+L, M+V, M+C, M+S) open the menu.
enum class MenuTarget : std::uint8_t { Main, Placements, Materials, Verifier, Config, Areas, AreaEditor, Placement, Loaded };
// Input only queues intent; the application chooses the menu implementation.
void requestMenuOpen(MenuTarget target = MenuTarget::Main);
std::optional<MenuTarget> consumeMenuOpenRequest();
void clearMenuOpenRequest();
}
