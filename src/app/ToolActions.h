#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <optional>
namespace lholo::app {
enum class ToolMode {AreaSelection,SchematicPlacement,Fill,ReplaceBlock,PasteSchematic,GridPaste,Move,Delete,Rebuild};
// Local integrated worlds can execute tool writes in Survival; remote command writes still need server permission.
constexpr ToolMode nextToolMode(ToolMode mode,bool backwards=false){
    return static_cast<ToolMode>((static_cast<int>(mode)+(backwards?8:1))%9);
}
struct ToolStatus {
    std::uint64_t revision{},done{},total{};
    ToolMode mode{ToolMode::AreaSelection};
    bool creative{},localWriteAvailable{},companionAvailable{},held{},running{};
    std::string primary{"minecraft:stone"},secondary{"minecraft:air"},error;
    std::optional<std::array<int,3>> moveTarget;
    std::array<int,3> gridNegative{},gridPositive{},gridStep{};
};
ToolStatus toolStatus();
bool queueToolCommand(std::string const&,std::string const& value={});
bool toolCommand(std::string const&,std::string const& value={});
void processToolActions();
void resetToolActions();
bool toolMouseInput(int button,bool down);
bool toolKeyInput(unsigned key);
// Ctrl + mouse wheel with the tool item: next / previous tool mode (Litematica's operation mode change modifier).
bool toolWheelInput(short delta, bool nudge = false);
}
