#pragma once
#include <functional>
#include <memory>
#include <set>
#include <string>
#include "config/ConfigRegistry.h"
#include "projection/core/OverlayStyle.h"
namespace lholo::app {
void loadJavaConfig(std::filesystem::path const& file = {});
void processJavaConfigActions();
void resetJavaConfigInput();
bool javaKeyInput(unsigned virtualKey,bool down,bool gui=false);
bool javaModifierHeld(std::string const& name);
bool javaConfigReady();
bool javaConfigCaptureActive();
bool queueJavaConfigCommand(std::string const& command,std::string const& value);
std::shared_ptr<config::Json const> javaConfigSnapshot();
struct JavaToolOptions {
    projection::OverlayStyle overlayStyle;
    bool ignoreCropAge{},ignoreBlocks{};std::set<std::string> ignorable;std::uint64_t rulesHash{};
    std::string item{"minecraft:stick"};
    bool rendering{true},enabled{true},requireHeld{true},reverseCycle{},expandCorners{},areaRendering{true};
    bool blocks{true},overlay{true},outlines{true},sides{true},throughBlocks{},placementBoxes{true};
};
std::shared_ptr<JavaToolOptions const> javaToolOptions();
// Registered by the plugin: actions that need the structure session (rotate, mirror, unload).
void setJavaStructureActionHook(std::function<bool(std::string const&)> hook);
// Bumped by the rerenderSchematic hotkey; the projection rescans everything when it changes.
std::uint64_t javaRerenderRevision();
}
