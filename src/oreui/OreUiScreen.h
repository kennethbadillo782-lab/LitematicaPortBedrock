#pragma once

namespace lholo::oreui {
inline constexpr char kMenuRoute[] = "/lholo-menu";
inline constexpr char kMenuAsset[] = "/hbui/lholo-menu.html";

bool installMenuScreen();
void uninstallMenuScreen();
// Called by the primary ClientInstance update; never retain a View or Scene.
void processMenuScreen();
enum class MenuEntryPage { Tasks, Hud, Placements, Materials, Verifier, Config, Areas, AreaEditor, Placement, Loaded };
void requestMenuOpen(MenuEntryPage page = MenuEntryPage::Tasks);
MenuEntryPage menuEntryPage();
void requestMenuClose();
bool isMenuOnTop();
// The page tells the game whether Esc should go back one screen (true) or close the menu (false).
void setMenuCanGoBack(bool canGoBack);
// True once per Esc press that must go back; consumed by the menu bridge.
bool takeMenuBack();
}
