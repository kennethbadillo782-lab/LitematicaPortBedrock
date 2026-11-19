#pragma once
#include "input/HotkeyTypes.h"
#include "i18n/TextKeys.h"
#include <array>
namespace lholo::ui {
using input::HotkeyId;
struct HotkeyDefinition { HotkeyId id; i18n::TextKey label; i18n::TextKey group; };
inline constexpr std::array<HotkeyDefinition, input::kHotkeyCount> kHotkeyDefinitions{{
    {HotkeyId::Gui, i18n::TextKey::HotkeyOpenMenu, i18n::TextKey::HotkeysGroupMenu},
    {HotkeyId::MoveLeft, i18n::TextKey::HotkeyMoveLeft, i18n::TextKey::HotkeysGroupMovement},
    {HotkeyId::MoveRight, i18n::TextKey::HotkeyMoveRight, i18n::TextKey::HotkeysGroupMovement},
    {HotkeyId::MoveForward, i18n::TextKey::HotkeyMoveForward, i18n::TextKey::HotkeysGroupMovement},
    {HotkeyId::MoveBackward, i18n::TextKey::HotkeyMoveBackward, i18n::TextKey::HotkeysGroupMovement},
    {HotkeyId::MoveUp, i18n::TextKey::HotkeyMoveUp, i18n::TextKey::HotkeysGroupMovement},
    {HotkeyId::MoveDown, i18n::TextKey::HotkeyMoveDown, i18n::TextKey::HotkeysGroupMovement},
    {HotkeyId::LayerIncrease, i18n::TextKey::HotkeyLayerIncrease, i18n::TextKey::HotkeysGroupLayers},
    {HotkeyId::LayerDecrease, i18n::TextKey::HotkeyLayerDecrease, i18n::TextKey::HotkeysGroupLayers},
    {HotkeyId::MaterialHudPreviousPage, i18n::TextKey::HotkeyMaterialHudPreviousPage, i18n::TextKey::HotkeysGroupMaterials},
    {HotkeyId::MaterialHudNextPage, i18n::TextKey::HotkeyMaterialHudNextPage, i18n::TextKey::HotkeysGroupMaterials},
    {HotkeyId::ExecuteOperation, i18n::TextKey::JavaHotkeyExecuteOperation, i18n::TextKey::HotkeysGroupMenu},
    {HotkeyId::NudgeSelectionNegative, i18n::TextKey::JavaHotkeyNudgeSelectionNegative, i18n::TextKey::HotkeysGroupMenu},
    {HotkeyId::NudgeSelectionPositive, i18n::TextKey::JavaHotkeyNudgeSelectionPositive, i18n::TextKey::HotkeysGroupMenu},
    {HotkeyId::AddSelectionBox, i18n::TextKey::JavaHotkeyAddSelectionBox, i18n::TextKey::HotkeysGroupMenu},
    {HotkeyId::DeleteSelectionBox, i18n::TextKey::JavaHotkeyDeleteSelectionBox, i18n::TextKey::HotkeysGroupMenu},
    {HotkeyId::SetAreaOrigin, i18n::TextKey::JavaHotkeySetAreaOrigin, i18n::TextKey::HotkeysGroupMenu},
    {HotkeyId::SetSelectionPosition1, i18n::TextKey::JavaHotkeySetSelectionPosition1, i18n::TextKey::HotkeysGroupMenu},
    {HotkeyId::SetSelectionPosition2, i18n::TextKey::JavaHotkeySetSelectionPosition2, i18n::TextKey::HotkeysGroupMenu},
    {HotkeyId::LayerModeNext, i18n::TextKey::JavaHotkeyLayerModeNext, i18n::TextKey::HotkeysGroupMenu},
    {HotkeyId::LayerModePrevious, i18n::TextKey::JavaHotkeyLayerModePrevious, i18n::TextKey::HotkeysGroupMenu},
    {HotkeyId::SelectionCornersMode, i18n::TextKey::JavaHotkeySelectionCornersMode, i18n::TextKey::HotkeysGroupMenu}
}};
}
