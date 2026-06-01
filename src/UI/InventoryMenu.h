#pragma once

namespace UI::InventoryMenu {
[[nodiscard]] bool LastOpenedMenuUsesVanillaBottomBar();
void QueueOpenMenuRingRowRefresh();
void QueueRingRowRefresh();
void QueueRefreshAfterVanillaRingSlotMove();
}
