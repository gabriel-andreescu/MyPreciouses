#pragma once

#include <RE/Skyrim.h> // IWYU pragma: keep

#include <cstdint>

namespace UI::FavoritesMenu {
enum class RowRefreshMode : std::uint8_t {
    kChangedRowsOnly,
    kForceRedraw,
};

[[nodiscard]] RE::FavoritesMenu* GetOpenMenu();
[[nodiscard]] bool GetItemList(RE::FavoritesMenu& a_menu, RE::GFxValue& a_itemList);
[[nodiscard]] RE::InventoryEntryData* GetRowEntry(RE::FavoritesMenu& a_menu, const RE::GFxValue& a_row);
void QueueRingRowRefresh(RowRefreshMode a_mode = RowRefreshMode::kChangedRowsOnly);
[[nodiscard]] bool TryRefreshOpenMenuRows(RowRefreshMode a_mode = RowRefreshMode::kChangedRowsOnly);
}
