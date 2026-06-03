#pragma once

namespace RE {
class InventoryMenu;
}

namespace UI::InventoryMenu {
[[nodiscard]] bool LastOpenedMenuUsesVanillaBottomBar();
void OnClosed();
void OnShown(RE::InventoryMenu& a_inventoryMenu);
void OnInventoryUpdateProcessed(RE::InventoryMenu& a_inventoryMenu);
void RefreshAfterNextInventoryUpdate();
void QueueInventoryListRefresh();
}
