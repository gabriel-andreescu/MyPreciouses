#include "UI/FavoritesMenu.h"

#include "UI/RingItemRows.h"
#include "UI/Scaleform.h"

#include <cstdint>

namespace UI::FavoritesMenu {
namespace {
    constexpr auto kScaleformItemFormID = "formId";
    constexpr auto kScaleformItemIndex = "index";

    [[nodiscard]] bool GetFavoritesItemList(RE::FavoritesMenu& a_menu, RE::GFxValue& a_itemList) {
        auto& root = a_menu.GetRuntimeData().root;
        if (root.GetMember("ItemList", std::addressof(a_itemList)) && Scaleform::CanReadMembers(a_itemList)) {
            return true;
        }

        return root.GetMember("itemList", std::addressof(a_itemList)) && Scaleform::CanReadMembers(a_itemList);
    }

    [[nodiscard]] RE::InventoryEntryData* GetFavoriteEntryDataForRow(
        RE::FavoritesMenu& a_menu,
        const RE::GFxValue& a_entryObject
    ) {
        const auto favoriteIndex = Scaleform::ReadUInt32Member(a_entryObject, kScaleformItemIndex);
        if (!favoriteIndex) {
            return nullptr;
        }

        auto& favorites = a_menu.GetRuntimeData().favorites;
        if (*favoriteIndex >= favorites.size()) {
            return nullptr;
        }

        auto* entry = favorites[*favoriteIndex].entryData;
        if (!entry || !entry->GetObject()) {
            return nullptr;
        }

        const auto formID = Scaleform::ReadUInt32Member(a_entryObject, kScaleformItemFormID);
        if (formID && entry->GetObject()->GetFormID() != *formID) {
            return nullptr;
        }

        return entry;
    }

    [[nodiscard]] RingItemRows::RowStampResult StampFavoriteRingEntry(
        RE::FavoritesMenu& a_menu,
        RE::GFxValue& a_entryObject
    ) {
        auto* entry = GetFavoriteEntryDataForRow(a_menu, a_entryObject);
        if (!entry) {
            return RingItemRows::RowStampResult::kIgnored;
        }

        return RingItemRows::StampRingEntry(a_entryObject, *entry, Core::GetPlayerActorKey(), false);
    }

    [[nodiscard]] bool RedrawFavoritesRows(RE::GFxValue& a_itemList) {
        if (a_itemList.Invoke("InvalidateData")) {
            return true;
        }

        return a_itemList.Invoke("requestInvalidate");
    }

    [[nodiscard]] RE::FavoritesMenu* GetOpenFavoritesMenu() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui || !ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME)) {
            return nullptr;
        }

        auto favoritesMenu = ui->GetMenu<RE::FavoritesMenu>();
        return favoritesMenu.get();
    }

    void RestampOpenFavoritesRows(const RowRefreshMode a_mode) {
        auto* favoritesMenu = GetOpenFavoritesMenu();
        if (!favoritesMenu) {
            return;
        }

        RE::GFxValue itemList;
        if (!GetFavoritesItemList(*favoritesMenu, itemList)) {
            return;
        }

        RE::GFxValue entryList;
        if (!itemList.GetMember("entryList", std::addressof(entryList)) || !entryList.IsArray()) {
            return;
        }

        std::uint32_t ringRows = 0;
        std::uint32_t changedEntryRows = 0;
        std::uint32_t changedSelectedRows = 0;

        for (std::uint32_t index = 0; index < entryList.GetArraySize(); ++index) {
            RE::GFxValue entryObject;
            if (!entryList.GetElement(index, std::addressof(entryObject)) || !Scaleform::CanReadMembers(entryObject)) {
                continue;
            }

            const auto result = StampFavoriteRingEntry(*favoritesMenu, entryObject);
            if (result == RingItemRows::RowStampResult::kIgnored) {
                continue;
            }

            ++ringRows;
            changedEntryRows += result == RingItemRows::RowStampResult::kChanged ? 1 : 0;
            static_cast<void>(entryList.SetElement(index, entryObject));
        }

        RE::GFxValue selectedEntry;
        if (itemList.GetMember("selectedEntry", std::addressof(selectedEntry))
            && Scaleform::CanReadMembers(selectedEntry)) {
            const auto result = StampFavoriteRingEntry(*favoritesMenu, selectedEntry);
            if (result != RingItemRows::RowStampResult::kIgnored) {
                changedSelectedRows += result == RingItemRows::RowStampResult::kChanged ? 1 : 0;
                static_cast<void>(itemList.SetMember("selectedEntry", selectedEntry));
            }
        }

        if (ringRows == 0) {
            return;
        }

        if (a_mode != RowRefreshMode::kForceRedraw && changedEntryRows == 0 && changedSelectedRows == 0) {
            return;
        }

        static_cast<void>(RedrawFavoritesRows(itemList));
    }
}

void QueueRingRowRefresh(const RowRefreshMode a_mode) {
    stl::add_ui_task([a_mode] {
        RestampOpenFavoritesRows(a_mode);
    });
}
}
