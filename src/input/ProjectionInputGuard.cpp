#include "app/JavaConfigActions.h"
// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "input/ProjectionInputGuard.h"
#include "input/WindowHotkeys.h"
#include "app/ToolActions.h"

#include "structure/StructureLoader.h"

#include "ll/api/memory/Hook.h"
#include <Windows.h>

#include "mc/deps/input/MouseAction.h"
#include "mc/deps/input/MouseDevice.h"

namespace lholo::input {
namespace {

bool gInstalled{};

LL_TYPE_INSTANCE_HOOK(
    ProjectionMouseInputHook,
    ll::memory::HookPriority::Highest,
    MouseDevice,
    &MouseDevice::feed,
    void,
    char  actionButtonId,
    schar buttonData,
    short x,
    short y,
    short dx,
    short dy,
    bool  forceMotionlessPointer
) {
    if(actionButtonId>=MouseAction::ActionLeft&&actionButtonId<=MouseAction::ActionMiddle){
        if(app::javaConfigReady()){
            if(!worldToolInputAllowed()){origin(actionButtonId,buttonData,x,y,dx,dy,forceMotionlessPointer);return;}
            if(buttonData!=MouseAction::DataUp)for(auto modifier:{VK_LCONTROL,VK_RCONTROL,VK_LMENU,VK_RMENU,VK_LSHIFT,VK_RSHIFT})
                app::javaKeyInput(modifier,(GetAsyncKeyState(modifier)&0x8000)!=0);
            auto key=actionButtonId==MouseAction::ActionLeft?VK_LBUTTON:actionButtonId==MouseAction::ActionRight?VK_RBUTTON:VK_MBUTTON;
            if(worldToolInputAllowed()&&app::javaKeyInput(key,buttonData!=MouseAction::DataUp))return;
        }else{
            if(buttonData==MouseAction::DataUp){if(app::toolMouseInput(actionButtonId,false))return;}
            else if(worldToolInputAllowed()&&app::toolMouseInput(actionButtonId,true))return;
        }
    }
    if (actionButtonId == MouseAction::ActionWheel && buttonData != 0 && worldToolInputAllowed()) {
        // Bedrock's MouseDevice is the wheel source, including raw mouse input.
        // Poll physical modifiers here when Windows did not deliver matching key messages.
        if(app::javaConfigReady()){
            for(auto modifier:{VK_LCONTROL,VK_RCONTROL,VK_LMENU,VK_RMENU,VK_LSHIFT,VK_RSHIFT})
                app::javaKeyInput(modifier,(GetAsyncKeyState(modifier)&0x8000)!=0);
        }
        bool nudge=app::javaConfigReady()?app::javaModifierHeld("selectionNudgeModifier"):(GetAsyncKeyState(VK_MENU)&0x8000)!=0;
        bool cycle=app::javaConfigReady()?app::javaModifierHeld("operationModeChangeModifier"):(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0;
        if((nudge||cycle)&&app::toolWheelInput(static_cast<short>(buttonData),nudge))return;
    }
    if (actionButtonId == MouseAction::ActionWheel && structure::scrollLockActive()) return;
    origin(actionButtonId, buttonData, x, y, dx, dy, forceMotionlessPointer);
}

} // namespace

bool installProjectionInputGuard() {
    if (!gInstalled) gInstalled = ProjectionMouseInputHook::hook() == 0;
    return gInstalled;
}

void uninstallProjectionInputGuard() {
    if (gInstalled) ProjectionMouseInputHook::unhook();
    gInstalled = false;
}

} // namespace lholo::input
