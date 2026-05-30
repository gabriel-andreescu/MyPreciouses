#pragma once

#include "Core/ActorKey.h"

#include <cstdint>

namespace Equipment {
struct ActionResult;
}

namespace UI {
enum class ItemMenuHost : std::uint8_t {
    kInventory,
    kFavorites,
};

void RegisterItemMenuDataCallback();
void HandleMenuOpenCloseEvent(const RE::MenuOpenCloseEvent& a_event);
void RefreshRingItemRows();
void RefreshItemRowsAfterEquipmentAction(
    ItemMenuHost a_hostMenu,
    Core::ActorKey a_actor,
    RE::FormID a_sourceFormID,
    Equipment::ActionResult a_result
);
void QueueFavoritesRefreshAfterRingEquip();
}
