#pragma once

namespace UI {
void InstallMenuEventSink();
void RegisterInventoryData();
void RefreshRows();
void RefreshEquipmentSoon(RE::FormID a_ringFormID);
[[nodiscard]] bool SelectEntryForLeftHand(RE::InventoryEntryData* a_entry);
}
