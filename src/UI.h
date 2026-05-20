#pragma once

namespace UI {
void InstallSinks();
void RegisterInventoryData();
void RefreshRows();
void RefreshEquipmentSoon(RE::FormID a_ringFormID);
[[nodiscard]] bool IsRightMouseDown(RE::InputEvent& a_event);
[[nodiscard]] bool SelectForLeftHand(RE::InventoryEntryData* a_entry);
[[nodiscard]] bool HandleRightClick();
[[nodiscard]] bool ConsumeFavoritesRightClick();
}
