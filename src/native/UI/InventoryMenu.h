#pragma once

#include <RE/Skyrim.h> // IWYU pragma: keep

namespace RE {
class Actor;
class InventoryMenu;
class TESObjectARMO;
}

namespace UI::InventoryMenu {
[[nodiscard]] bool LastOpenedMenuUsesVanillaBottomBar();
void OnClosed();
void OnShown(RE::InventoryMenu& a_inventoryMenu);
void OnInventoryUpdateProcessed(RE::InventoryMenu& a_inventoryMenu);
[[nodiscard]] bool TryRefreshOpenMenuRows();
[[nodiscard]] bool TryRefreshOpenMenuRowsForRing(RE::Actor& a_actor, const RE::TESObjectARMO& a_ring);
}
