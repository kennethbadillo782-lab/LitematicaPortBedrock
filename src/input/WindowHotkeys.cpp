#include "app/JavaConfigActions.h"
// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "input/WindowHotkeys.h"
#include "app/ToolActions.h"

#include "input/HotkeyCapture.h"
#include "input/HotkeyContext.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/ClientInstanceState.h"
#include "mc/client/gui/screens/AbstractScene.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"
#include "mc/gamecore/AppPlatform_GameCore.h"
#include "oreui/OreUiScreen.h"
#include "structure/StructureLoader.h"
#include "structure/StructureUiState.h"
#include "ui/hud/editor/HudEditor.h"
#include "ui/preview/StructurePreviewScene.h"

#include <Windows.h>
#include <windowsx.h>
#include <array>
#include <atomic>

namespace lholo::input {
namespace {
HWND installedWindow{};
std::atomic<WNDPROC> originalWndProc{};
std::atomic_bool projectionHotkeysAllowed{};
std::array<bool, 256> captureKeyReleases{}; // Window thread only.

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto const original = originalWndProc.load();
    if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && wParam == FALSE)) {
        captureKeyReleases.fill(false);
        app::resetJavaConfigInput();
        structure::resetHotkeyState();
    }
    unsigned captureKey{};
    bool captureDown{}, captureUp{};
    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) { captureKey = static_cast<unsigned>(wParam); captureDown = true; }
    if (message == WM_KEYUP || message == WM_SYSKEYUP) { captureKey = static_cast<unsigned>(wParam); captureUp = true; }
    if(captureDown||captureUp){
        if(captureKey==VK_SHIFT){auto side=MapVirtualKeyW((static_cast<unsigned long long>(lParam)>>16)&0xff,MAPVK_VSC_TO_VK_EX);if(side)captureKey=side;}
        else if(captureKey==VK_CONTROL)captureKey=(lParam&(1LL<<24))?VK_RCONTROL:VK_LCONTROL;
        else if(captureKey==VK_MENU)captureKey=(lParam&(1LL<<24))?VK_RMENU:VK_LMENU;
        else if(captureKey==VK_RETURN&&(lParam&(1LL<<24)))captureKey=VK_SEPARATOR; // Native keypad Enter, separate from Return.
    }
    if(app::javaConfigCaptureActive()&&(message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||message==WM_RBUTTONDOWN||message==WM_RBUTTONUP)){
        captureKey=(message==WM_LBUTTONDOWN||message==WM_LBUTTONUP)?VK_LBUTTON:VK_RBUTTON;
        captureDown=message==WM_LBUTTONDOWN||message==WM_RBUTTONDOWN;captureUp=!captureDown;
    }
    if (message == WM_MBUTTONDOWN || message == WM_MBUTTONUP) {
        captureKey = VK_MBUTTON; captureDown = message == WM_MBUTTONDOWN; captureUp = !captureDown;
    }
    if (message == WM_XBUTTONDOWN || message == WM_XBUTTONUP) {
        captureKey = GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
        captureDown = message == WM_XBUTTONDOWN; captureUp = !captureDown;
    }
    if (captureUp && captureKey < captureKeyReleases.size() && captureKeyReleases[captureKey]) {
        captureKeyReleases[captureKey] = false;
        app::javaKeyInput(captureKey,false,oreui::isMenuOnTop());
        structure::handleGuiHotkeyKeyUp(captureKey);
        return 1;
    }
    if (captureDown && captureKey < captureKeyReleases.size() && captureKeyReleases[captureKey]) return 1;
    if (oreui::isMenuOnTop() || ui::hud::hudLayoutEditorActive() || ui::preview::structurePreviewActive()) {
        if((captureDown||captureUp)&&app::javaConfigCaptureActive()){app::javaKeyInput(captureKey,captureDown,true);return 1;}
        if (captureDown && input::hotkeyCaptureActive()) {
            auto modifiers = ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? 1u : 0u)
                | ((GetAsyncKeyState(VK_MENU) & 0x8000) ? 2u : 0u)
                | ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 4u : 0u);
            if (input::captureHotkeyKey(captureKey, modifiers)) {
                if (captureKey < captureKeyReleases.size()) captureKeyReleases[captureKey] = true;
                return 1;
            }
        }
        if((captureDown||captureUp)&&oreui::isMenuOnTop()&&!input::hotkeyCaptureActive()&&app::javaKeyInput(captureKey,captureDown,true)){
            if(captureDown&&captureKey<captureKeyReleases.size())captureKeyReleases[captureKey]=true;
            return 1;
        }
        if (captureDown && oreui::isMenuOnTop() && !input::hotkeyCaptureActive()) {
            auto const binding = structure::detail::StructureUiState::getInstance().inputHotkey(
                hotkeyIndex(HotkeyId::Gui));
            auto const modifiers = ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? 1u : 0u)
                | ((GetAsyncKeyState(VK_MENU) & 0x8000) ? 2u : 0u)
                | ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 4u : 0u);
            // A bare letter must remain available to every text field.
            // Escape is the unconditional close action while editing.
            if (binding.key && captureKey == binding.key && binding.modifiers == modifiers
                && modifiers != 0) {
                oreui::requestMenuClose();
                captureKeyReleases[captureKey] = true;
                return 1;
            }
        }
        return CallWindowProcW(original, window, message, wParam, lParam);
    }
    if (!projectionHotkeysAllowed.load(std::memory_order_acquire))
        return CallWindowProcW(original, window, message, wParam, lParam);
    if(captureDown&&app::javaConfigReady()&&!app::javaConfigCaptureActive()){
        // Raw keyboard paths can omit modifier messages; reconcile before evaluating Ctrl+V, Alt+key, etc.
        for(auto modifier:{VK_LCONTROL,VK_RCONTROL,VK_LMENU,VK_RMENU,VK_LSHIFT,VK_RSHIFT})
            if(captureKey!=static_cast<unsigned>(modifier))app::javaKeyInput(modifier,(GetAsyncKeyState(modifier)&0x8000)!=0);
    }
    if((captureDown||captureUp)&&app::javaKeyInput(captureKey,captureDown)){
        if(captureDown&&captureKey<captureKeyReleases.size())captureKeyReleases[captureKey]=true;
        return 1;
    }
    if (captureDown && (structure::handleGuiHotkeyKeyDown(captureKey)||app::toolKeyInput(captureKey))) {
        if (captureKey < captureKeyReleases.size()) captureKeyReleases[captureKey] = true;
        return 1;
    }
    if (captureUp && structure::handleGuiHotkeyKeyUp(captureKey)) return 1;
    // Tool wheel handling lives in MouseDevice::feed, avoiding double cycles and supporting raw input.
    if (message == WM_MOUSEWHEEL && structure::handleProjectionOffsetWheel(GET_WHEEL_DELTA_WPARAM(wParam)))
        return 1;
    return CallWindowProcW(original, window, message, wParam, lParam);
}
}

bool worldToolInputAllowed(){return projectionHotkeysAllowed.load(std::memory_order_acquire);}
void updateWindowHotkeys() {
    // Sample scene ownership on the client logic thread. WndProc only reads
    // this atomic, never the scene stack or world-owned pointers.
    bool allowed{};
    if (auto client = ll::service::getClientInstance()) {
        auto const state = client->$getClientInstanceState();
        ::Bedrock::NonOwnerPointer<ISceneStack> stack = client->$getCurrentSceneStack();
        auto* top = stack ? stack->getTopScene() : nullptr;
        allowed = top && acceptsProjectionHotkeys(state == ClientInstanceState::Playing,
            state == ClientInstanceState::Idle, top->getScreenName());
    }
    if (projectionHotkeysAllowed.exchange(allowed, std::memory_order_acq_rel) && !allowed){
        structure::resetHotkeyState();app::resetJavaConfigInput();
    }
    auto platform = ll::service::getAppPlatform();
    if (!platform) return;
    auto const window = static_cast<AppPlatform_GameCore&>(*platform).mHWnd;
    if (!window || window == installedWindow) return;
    uninstallWindowHotkeys();

    auto const previous = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
    if (!previous) return;
    originalWndProc = previous;
    if (auto const replaced = SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(windowProc))) {
        originalWndProc = reinterpret_cast<WNDPROC>(replaced);
        installedWindow = window;
    }
}

void uninstallWindowHotkeys() {
    projectionHotkeysAllowed.store(false, std::memory_order_release);
    if (!installedWindow) return;
    if (IsWindow(installedWindow))
        SetWindowLongPtrW(installedWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalWndProc.load()));
    installedWindow = nullptr;
    structure::resetHotkeyState();
}
}
