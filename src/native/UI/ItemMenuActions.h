#pragma once

#include <RE/Skyrim.h> // IWYU pragma: keep

#include "Core/ActorKey.h"
#include "Core/Target.h"
#include "UI.h"

namespace UI::ItemMenuActions {
[[nodiscard]] bool HandleRingUseFromMenuEntry(
    RE::InventoryEntryData* a_entry,
    Core::Hand a_hand,
    ItemMenuHost a_host,
    Core::ActorKey a_itemActor
);
}
