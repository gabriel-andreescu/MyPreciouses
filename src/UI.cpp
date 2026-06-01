#include "UI.h"

#include "Equipment/AssignmentActions.h"
#include "Inventory.h"
#include "UI/FavoritesMenu.h"
#include "UI/FingerSelectMenu.h"
#include "UI/InventoryMenu.h"
#include "UI/RingItemRows.h"
#include "UI/VanillaItemMenuControls.h"

#include <RE/S/SendUIMessage.h>

namespace UI {
namespace {
    [[nodiscard]] bool IsOpenInventoryMenuMovie(const RE::GFxMovieView* a_view) {
        if (!a_view) {
            return false;
        }

        auto* ui = RE::UI::GetSingleton();
        if (!ui || !ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME)) {
            return false;
        }

        auto inventoryMenu = ui->GetMenu<RE::InventoryMenu>();
        return inventoryMenu && inventoryMenu->uiMovie.get() == a_view;
    }

    void StampItemMenuRingRowData(RE::GFxMovieView* a_view, RE::GFxValue* a_object, RE::InventoryEntryData* a_item) {
        if (!a_object || !a_item) {
            return;
        }

        static_cast<void>(RingItemRows::StampRingEntry(
            *a_object,
            *a_item,
            Core::GetPlayerActorKey(),
            IsOpenInventoryMenuMovie(a_view)
        ));
    }

    void RefreshRingItemRowsForRing(RE::Actor& a_actor, const RE::TESObjectARMO* a_ring) {
        RE::SendUIMessage::SendInventoryUpdateMessage(std::addressof(a_actor), a_ring);
        FavoritesMenu::QueueRingRowRefresh();
    }
}

void RegisterItemMenuDataCallback() {
    if (const auto* scaleform = SKSE::GetScaleformInterface()) {
        scaleform->Register(StampItemMenuRingRowData);
        logger::info("UI: item menu data callback registered");
    } else {
        logger::warn("UI: item menu data callback skipped | reason=noScaleform");
    }
}

void HandleMenuOpenCloseEvent(const RE::MenuOpenCloseEvent& a_event) {
    if (a_event.opening) {
        if (a_event.menuName == RE::InventoryMenu::MENU_NAME.data()) {
            InventoryMenu::QueueOpenMenuRingRowRefresh();
        } else if (a_event.menuName == RE::FavoritesMenu::MENU_NAME.data()) {
            FavoritesMenu::QueueRingRowRefresh();
        }

        return;
    }

    FingerSelectMenu::OnMenuClose(a_event.menuName);
}

void RefreshRingItemRows() {
    InventoryMenu::QueueRingRowRefresh();
    FavoritesMenu::QueueRingRowRefresh();
}

bool ShouldWarnUnsupportedVanillaInventoryHint(const std::uint32_t a_controllerButton) {
    return InventoryMenu::LastOpenedMenuUsesVanillaBottomBar()
           && !VanillaItemMenuControls::SupportsControllerButtonArt(a_controllerButton);
}

void RefreshItemRowsAfterEquipmentAction(
    const ItemMenuHost a_hostMenu,
    const Core::ActorKey a_actor,
    const RE::FormID a_sourceFormID,
    const Equipment::ActionResult a_result
) {
    auto* actor = Core::ResolveActor(a_actor);
    auto* ring = Inventory::AsRing(RE::TESForm::LookupByID<RE::TESObjectARMO>(a_sourceFormID));

    if (a_result.inventoryChanged) {
        if (a_hostMenu == ItemMenuHost::kInventory) {
            InventoryMenu::QueueRefreshAfterVanillaRingSlotMove();
        } else if (actor && ring) {
            RefreshRingItemRowsForRing(*actor, ring);
        }
        return;
    }

    if (a_result.sourceUnavailable) {
        if (actor && ring) {
            RefreshRingItemRowsForRing(*actor, ring);
        }
        return;
    }

    if (a_result.selectionChanged) {
        RefreshRingItemRows();
    }
}

void QueueFavoritesRefreshAfterRingEquip() {
    stl::add_task([] {
        FavoritesMenu::QueueRingRowRefresh();
    });
}
}
