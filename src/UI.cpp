#include "UI.h"

#include "Inventory.h"
#include "RingSounds.h"
#include "Selection.h"
#include "VirtualRings.h"

#include <RE/S/SendUIMessage.h>

#include <algorithm>
#include <utility>

namespace UI {
namespace {
    constexpr auto kSkyUIEquipStateNone = 0;
    constexpr auto kSkyUIEquipStateLeft = 2;
    constexpr auto kSkyUIEquipStateRight = 3;
    constexpr auto kSkyUIEquipStateBoth = 4;
    constexpr auto kScaleformSelectionKind = "lhrsSelectionKind";
    constexpr auto kScaleformCustomEnchantmentID = "lhrsCustomEnchantmentID";
    constexpr auto kScaleformCustomCharge = "lhrsCustomCharge";
    constexpr auto kScaleformCustomRemoveOnUnequip = "lhrsCustomRemoveOnUnequip";
    constexpr auto kScaleformCustomDisplayName = "lhrsCustomDisplayName";
    constexpr auto kScaleformCustomHasUniqueID = "lhrsCustomHasUniqueID";
    constexpr auto kScaleformCustomUniqueBaseID = "lhrsCustomUniqueBaseID";
    constexpr auto kScaleformCustomUniqueID = "lhrsCustomUniqueID";
    constexpr auto kScaleformCustomBlockReason = "lhrsCustomBlockReason";
    constexpr auto kScaleformVanillaRingSlotEquipped = "lhrsVanillaRingSlotEquipped";
    constexpr auto kInventoryInvalidateListDataPath = "_root.Menu_mc.inventoryLists.InvalidateListData";

    struct RowSelection {
        Selection::Kind kind {Selection::Kind::kNone};
        std::optional<Inventory::CustomEnchantmentKey> customKey;
        std::optional<Inventory::ExtraListIdentity> customIdentity;
    };

    struct LeftSelectionRequest {
        RE::TESObjectARMO* ring {nullptr};
        std::optional<Inventory::CustomEnchantmentKey> customKey;
        std::optional<Inventory::ExtraListIdentity> customIdentity;
        RE::ExtraDataList* sourceExtraList {nullptr};
        bool blocked {false};
    };

    [[nodiscard]] RE::InventoryMenu* GetInventoryMenu() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui || !ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME)) {
            return nullptr;
        }

        auto inventoryMenu = ui->GetMenu<RE::InventoryMenu>();
        return inventoryMenu.get();
    }

    [[nodiscard]] RE::FavoritesMenu* GetFavoritesMenu() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui || !ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME)) {
            return nullptr;
        }

        auto favoritesMenu = ui->GetMenu<RE::FavoritesMenu>();
        return favoritesMenu.get();
    }

    [[nodiscard]] bool GetFavoritesItemList(RE::FavoritesMenu& a_menu, RE::GFxValue& a_itemList) {
        auto& root = a_menu.GetRuntimeData().root;
        if (root.GetMember("ItemList", std::addressof(a_itemList))
            && (a_itemList.IsObject() || a_itemList.IsDisplayObject())) {
            return true;
        }

        return root.GetMember("itemList", std::addressof(a_itemList))
               && (a_itemList.IsObject() || a_itemList.IsDisplayObject());
    }

    [[nodiscard]] bool IsLeftEquipped(const RE::TESObjectARMO& a_ring, const RowSelection& a_rowSelection) {
        const auto selection = Selection::Get(kDefaultLeftEquipTarget);
        const auto formID = a_ring.GetFormID();
        if (selection.sourceFormID == 0 || selection.sourceFormID != formID) {
            return false;
        }

        if (selection.MatchesForm(formID)) {
            return a_rowSelection.kind == Selection::Kind::kFormOnly;
        }

        return a_rowSelection.kind
               == Selection::Kind::kCustomEnchantment
               && a_rowSelection.customKey
               && selection.MatchesCustomEnchantment(formID, *a_rowSelection.customKey, a_rowSelection.customIdentity);
    }

    [[nodiscard]] int GetRingEquipState(
        const RE::TESObjectARMO& a_ring,
        const RowSelection& a_rowSelection,
        const bool a_vanillaRingSlotEquipped
    ) {
        const auto leftEquipped = IsLeftEquipped(a_ring, a_rowSelection);

        if (leftEquipped && a_vanillaRingSlotEquipped) {
            return kSkyUIEquipStateBoth;
        }

        if (leftEquipped) {
            return kSkyUIEquipStateLeft;
        }

        if (a_vanillaRingSlotEquipped) {
            return kSkyUIEquipStateRight;
        }

        return kSkyUIEquipStateNone;
    }

    [[nodiscard]] bool IsFormRowInVanillaRingSlot(RE::InventoryEntryData& a_entry) {
        if (!a_entry.extraLists) {
            return a_entry.IsWorn(false);
        }

        return std::ranges::any_of(*a_entry.extraLists, [](const auto* extraList) {
            return Inventory::IsRightWorn(extraList) && !Inventory::HasCustomEnchantment(extraList);
        });
    }

    [[nodiscard]] std::optional<RE::FormID> GetScaleformFormID(const RE::GFxValue& a_object) {
        if (!a_object.IsObject() && !a_object.IsDisplayObject()) {
            return std::nullopt;
        }

        RE::GFxValue formID;
        if (!a_object.GetMember("formId", std::addressof(formID)) || !formID.IsNumber()) {
            return std::nullopt;
        }

        return static_cast<RE::FormID>(formID.GetNumber());
    }

    [[nodiscard]] std::optional<std::uint32_t> GetScaleformIndex(const RE::GFxValue& a_object) {
        RE::GFxValue index;
        if (!a_object.GetMember("index", std::addressof(index)) || !index.IsNumber()) {
            return std::nullopt;
        }

        const auto value = static_cast<std::int32_t>(index.GetNumber());
        if (value < 0) {
            return std::nullopt;
        }

        return static_cast<std::uint32_t>(value);
    }

    [[nodiscard]] int GetScaleformEquipState(const RE::GFxValue& a_object) {
        RE::GFxValue equipState;
        if (!a_object.GetMember("equipState", std::addressof(equipState)) || !equipState.IsNumber()) {
            return kSkyUIEquipStateNone;
        }

        return static_cast<int>(equipState.GetNumber());
    }

    [[nodiscard]] int GetScaleformIntMember(const RE::GFxValue& a_object, const char* a_member, const int a_default) {
        RE::GFxValue value;
        if (!a_object.GetMember(a_member, std::addressof(value)) || !value.IsNumber()) {
            return a_default;
        }

        return static_cast<int>(value.GetNumber());
    }

    [[nodiscard]] std::optional<bool> GetScaleformBoolMember(const RE::GFxValue& a_object, const char* a_member) {
        RE::GFxValue value;
        if (!a_object.GetMember(a_member, std::addressof(value)) || !value.IsBool()) {
            return std::nullopt;
        }

        return value.GetBool();
    }

    [[nodiscard]] std::string GetScaleformStringMember(const RE::GFxValue& a_object, const char* a_member) {
        RE::GFxValue value;
        if (!a_object.GetMember(a_member, std::addressof(value)) || !value.IsString()) {
            return {};
        }

        const auto* string = value.GetString();
        return string ? string : "";
    }

    [[nodiscard]] RowSelection GetScaleformRowSelection(const RE::GFxValue& a_object) {
        const auto selectionKind = static_cast<Selection::Kind>(GetScaleformIntMember(
            a_object,
            kScaleformSelectionKind,
            static_cast<int>(std::to_underlying(Selection::Kind::kNone))
        ));

        if (selectionKind != Selection::Kind::kCustomEnchantment) {
            return RowSelection {
                .kind = selectionKind,
            };
        }

        RE::GFxValue enchantmentID;
        RE::GFxValue charge;
        RE::GFxValue removeOnUnequip;
        if (!a_object.GetMember(kScaleformCustomEnchantmentID, std::addressof(enchantmentID))
            || !enchantmentID.IsNumber()) {
            return RowSelection {
                .kind = Selection::Kind::kNone,
            };
        }

        if (!a_object.GetMember(kScaleformCustomCharge, std::addressof(charge)) || !charge.IsNumber()) {
            return RowSelection {
                .kind = Selection::Kind::kNone,
            };
        }

        if (!a_object.GetMember(kScaleformCustomRemoveOnUnequip, std::addressof(removeOnUnequip))
            || !removeOnUnequip.IsBool()) {
            return RowSelection {
                .kind = Selection::Kind::kNone,
            };
        }

        std::optional<Inventory::ExtraListIdentity> customIdentity;
        if (const auto hasUniqueID = GetScaleformBoolMember(a_object, kScaleformCustomHasUniqueID);
            hasUniqueID.value_or(false)) {
            RE::GFxValue uniqueBaseID;
            RE::GFxValue uniqueID;
            if (!a_object.GetMember(kScaleformCustomUniqueBaseID, std::addressof(uniqueBaseID))
                || !uniqueBaseID.IsNumber()
                || !a_object.GetMember(kScaleformCustomUniqueID, std::addressof(uniqueID))
                || !uniqueID.IsNumber()) {
                return RowSelection {
                    .kind = Selection::Kind::kNone,
                };
            }

            const auto identity = Inventory::ExtraListIdentity {
                .baseID = static_cast<RE::FormID>(uniqueBaseID.GetNumber()),
                .uniqueID = static_cast<std::uint16_t>(uniqueID.GetNumber()),
            };
            if (!identity.IsValid()) {
                return RowSelection {
                    .kind = Selection::Kind::kNone,
                };
            }

            customIdentity = identity;
        }

        return RowSelection {
            .kind = Selection::Kind::kCustomEnchantment,
            .customKey = Inventory::CustomEnchantmentKey {
                .enchantmentFormID = static_cast<RE::FormID>(enchantmentID.GetNumber()),
                .charge = static_cast<std::uint16_t>(charge.GetNumber()),
                .removeOnUnequip = removeOnUnequip.GetBool(),
                .playerDisplayName = GetScaleformStringMember(a_object, kScaleformCustomDisplayName),
            },
            .customIdentity = customIdentity,
        };
    }

    void StampScaleformSelection(
        RE::GFxValue& a_object,
        const Selection::Kind a_kind,
        const std::optional<Inventory::CustomEnchantmentKey>& a_customKey,
        const std::optional<Inventory::ExtraListIdentity>& a_customIdentity,
        const Inventory::EntryCustomFailure a_failure
    ) {
        a_object.SetMember(kScaleformSelectionKind, std::to_underlying(a_kind));
        a_object.SetMember(kScaleformCustomEnchantmentID, a_customKey ? a_customKey->enchantmentFormID : 0);
        a_object.SetMember(kScaleformCustomCharge, a_customKey ? a_customKey->charge : 0);
        a_object.SetMember(kScaleformCustomRemoveOnUnequip, a_customKey && a_customKey->removeOnUnequip);
        a_object.SetMember(kScaleformCustomHasUniqueID, a_customIdentity.has_value());
        a_object.SetMember(kScaleformCustomUniqueBaseID, a_customIdentity ? a_customIdentity->baseID : 0);
        a_object.SetMember(kScaleformCustomUniqueID, a_customIdentity ? a_customIdentity->uniqueID : 0);

        RE::GFxValue displayName;
        displayName.SetString(a_customKey ? std::string_view {a_customKey->playerDisplayName} : std::string_view {});
        a_object.SetMember(kScaleformCustomDisplayName, displayName);
        a_object.SetMember(kScaleformCustomBlockReason, std::to_underlying(a_failure));
    }

    [[nodiscard]] std::optional<Inventory::ExtraListIdentity> EnsureCustomSelectionIdentity(
        RE::TESObjectARMO& a_ring,
        Inventory::EntryCustomSelection& a_customSelection
    ) {
        if (!a_customSelection.HasCustomEnchantment() || !a_customSelection.extraList) {
            return std::nullopt;
        }

        if (a_customSelection.identity) {
            return a_customSelection.identity;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            logger::warn(
                "UI: custom selection identity skipped | form={:08X} | extraList={} | reason=noPlayer",
                a_ring.GetFormID(),
                static_cast<const void*>(a_customSelection.extraList)
            );
            return std::nullopt;
        }

        a_customSelection.identity = Inventory::EnsureExtraListIdentity(*player, a_ring, *a_customSelection.extraList);
        return a_customSelection.identity;
    }

    [[nodiscard]] std::optional<bool> StampEntryRingObject(RE::GFxValue& a_object, RE::InventoryEntryData& a_entry) {
        auto* ring = Inventory::AsRing(a_entry.GetObject());
        if (!ring) {
            return std::nullopt;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || Inventory::GetCount(*player, *ring) <= 0) {
            return std::nullopt;
        }

        auto customSelection = Inventory::ResolveEntryCustomSelection(a_entry);
        auto selectionKind = Selection::Kind::kFormOnly;
        std::optional<Inventory::CustomEnchantmentKey> customKey;
        std::optional<Inventory::ExtraListIdentity> customIdentity;
        auto vanillaRingSlotEquipped = IsFormRowInVanillaRingSlot(a_entry);

        if (customSelection.failure != Inventory::EntryCustomFailure::kNone) {
            selectionKind = Selection::Kind::kNone;
        } else if (customSelection.HasCustomEnchantment()) {
            customIdentity = EnsureCustomSelectionIdentity(*ring, customSelection);
            if (customIdentity) {
                selectionKind = Selection::Kind::kCustomEnchantment;
                customKey = customSelection.key;
                vanillaRingSlotEquipped = Inventory::IsRightWorn(customSelection.extraList);
            } else {
                selectionKind = Selection::Kind::kNone;
            }
        }

        StampScaleformSelection(a_object, selectionKind, customKey, customIdentity, customSelection.failure);

        const auto equipState = GetRingEquipState(
            *ring,
            RowSelection {
                .kind = selectionKind,
                .customKey = customKey,
                .customIdentity = customIdentity,
            },
            vanillaRingSlotEquipped
        );
        const auto previousEquipState = GetScaleformEquipState(a_object);
        a_object.SetMember(kScaleformVanillaRingSlotEquipped, vanillaRingSlotEquipped);
        a_object.SetMember("equipState", equipState);
        a_object.SetMember("isEquipped", equipState > kSkyUIEquipStateNone);

        return previousEquipState != equipState;
    }

    [[nodiscard]] RE::InventoryEntryData* GetFavoritesEntryDataForRow(
        RE::FavoritesMenu& a_menu,
        const RE::GFxValue& a_entryObject
    ) {
        const auto favoriteIndex = GetScaleformIndex(a_entryObject);
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

        const auto formID = GetScaleformFormID(a_entryObject);
        if (formID && entry->GetObject()->GetFormID() != *formID) {
            return nullptr;
        }

        return entry;
    }

    [[nodiscard]] std::optional<bool> StampFavoritesEntryObject(
        RE::FavoritesMenu& a_menu,
        RE::GFxValue& a_entryObject
    ) {
        auto* entry = GetFavoritesEntryDataForRow(a_menu, a_entryObject);
        if (!entry) {
            return std::nullopt;
        }

        return StampEntryRingObject(a_entryObject, *entry);
    }

    void InventoryDataCallback(
        [[maybe_unused]] RE::GFxMovieView* a_view,
        RE::GFxValue* a_object,
        RE::InventoryEntryData* a_item
    ) {
        if (!a_object || !a_item) {
            return;
        }

        static_cast<void>(StampEntryRingObject(*a_object, *a_item));
    }

    [[nodiscard]] std::optional<bool> RestampInventoryRowFromStoredSelection(RE::GFxValue& a_entryObject) {
        const auto formID = GetScaleformFormID(a_entryObject);
        if (!formID) {
            return std::nullopt;
        }

        auto* ring = Inventory::AsRing(RE::TESForm::LookupByID<RE::TESObjectARMO>(*formID));
        if (!ring) {
            return std::nullopt;
        }

        const auto previousEquipState = GetScaleformEquipState(a_entryObject);
        const auto previousVanillaRingSlotState = previousEquipState
                                                  == kSkyUIEquipStateRight
                                                  || previousEquipState
                                                  == kSkyUIEquipStateBoth;
        const auto storedVanillaRingSlotState = GetScaleformBoolMember(
            a_entryObject,
            kScaleformVanillaRingSlotEquipped
        );
        const auto rowSelection = GetScaleformRowSelection(a_entryObject);
        const auto vanillaRingSlotEquipped = storedVanillaRingSlotState.value_or(previousVanillaRingSlotState);
        const auto equipState = GetRingEquipState(*ring, rowSelection, vanillaRingSlotEquipped);

        a_entryObject.SetMember(kScaleformVanillaRingSlotEquipped, vanillaRingSlotEquipped);
        a_entryObject.SetMember("equipState", equipState);
        a_entryObject.SetMember("isEquipped", equipState > kSkyUIEquipStateNone);

        return previousEquipState != equipState;
    }

    [[nodiscard]] bool InvalidateInventoryListData(RE::InventoryMenu& a_menu) {
        auto* movie = a_menu.uiMovie.get();
        if (!movie) {
            return false;
        }

        if (!movie->IsAvailable(kInventoryInvalidateListDataPath)) {
            return false;
        }

        return movie->Invoke(kInventoryInvalidateListDataPath, nullptr, nullptr, 0);
    }

    [[nodiscard]] bool RestampInventoryRowsFromLiveState() {
        auto* inventoryMenu = GetInventoryMenu();
        if (!inventoryMenu) {
            return false;
        }

        auto* itemList = inventoryMenu->GetRuntimeData().itemList;
        if (!itemList || !itemList->entryList.IsArray()) {
            return false;
        }

        std::uint32_t changedEntryRows = 0;
        for (std::uint32_t index = 0; index < itemList->entryList.GetArraySize(); ++index) {
            RE::GFxValue entryObject;
            if (!itemList->entryList.GetElement(index, std::addressof(entryObject))
                || (!entryObject.IsObject() && !entryObject.IsDisplayObject())) {
                continue;
            }

            const auto changed = RestampInventoryRowFromStoredSelection(entryObject);
            if (!changed) {
                continue;
            }

            if (!*changed) {
                continue;
            }

            ++changedEntryRows;
            static_cast<void>(itemList->entryList.SetElement(index, entryObject));
        }

        return changedEntryRows > 0 && InvalidateInventoryListData(*inventoryMenu);
    }

    void DeselectInventoryItem(RE::InventoryMenu& a_menu) {
        auto* itemList = a_menu.GetRuntimeData().itemList;
        if (!itemList) {
            return;
        }

        static_cast<void>(itemList->root.SetMember("selectedIndex", RE::GFxValue {-1.0}));
    }

    [[nodiscard]] bool RedrawFavoritesRows(RE::GFxValue& a_itemList) {
        const auto invalidated = a_itemList.Invoke("InvalidateData");
        if (invalidated) {
            return true;
        }

        const auto requested = a_itemList.Invoke("requestInvalidate");
        return requested;
    }

    [[nodiscard]] bool RestampFavoritesRowsFromEntryData() {
        auto* favoritesMenu = GetFavoritesMenu();
        if (!favoritesMenu) {
            return false;
        }

        RE::GFxValue itemList;
        if (!GetFavoritesItemList(*favoritesMenu, itemList)) {
            return false;
        }

        RE::GFxValue entryList;
        if (!itemList.GetMember("entryList", std::addressof(entryList)) || !entryList.IsArray()) {
            return false;
        }

        std::uint32_t ringRows = 0;
        std::uint32_t changedEntryRows = 0;
        std::uint32_t changedSelectedRows = 0;

        for (std::uint32_t index = 0; index < entryList.GetArraySize(); ++index) {
            RE::GFxValue entryObject;
            if (!entryList.GetElement(index, std::addressof(entryObject))
                || (!entryObject.IsObject() && !entryObject.IsDisplayObject())) {
                continue;
            }

            const auto changed = StampFavoritesEntryObject(*favoritesMenu, entryObject);
            if (!changed) {
                continue;
            }

            ++ringRows;
            changedEntryRows += *changed ? 1 : 0;
            static_cast<void>(entryList.SetElement(index, entryObject));
        }

        RE::GFxValue selectedEntry;
        if (itemList.GetMember("selectedEntry", std::addressof(selectedEntry))
            && (selectedEntry.IsObject() || selectedEntry.IsDisplayObject())) {
            if (const auto changed = StampFavoritesEntryObject(*favoritesMenu, selectedEntry)) {
                changedSelectedRows += *changed ? 1 : 0;
                static_cast<void>(itemList.SetMember("selectedEntry", selectedEntry));
            }
        }

        if (ringRows == 0) {
            return false;
        }

        if (changedEntryRows == 0 && changedSelectedRows == 0) {
            return false;
        }

        return RedrawFavoritesRows(itemList);
    }

    void QueueInventoryRefresh() {
        stl::add_ui_task([] {
            static_cast<void>(RestampInventoryRowsFromLiveState());
        });
    }

    [[nodiscard]] std::optional<LeftSelectionRequest> BuildSelectionFromEntry(RE::InventoryEntryData& a_entry) {
        auto* ring = Inventory::AsRing(a_entry.GetObject());
        if (!ring) {
            return std::nullopt;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || Inventory::GetCount(*player, *ring) <= 0) {
            return std::nullopt;
        }

        auto customSelection = Inventory::ResolveEntryCustomSelection(a_entry);
        if (customSelection.failure != Inventory::EntryCustomFailure::kNone) {
            return LeftSelectionRequest {
                .ring = ring,
                .blocked = true,
            };
        }

        if (customSelection.HasCustomEnchantment()) {
            const auto customIdentity = EnsureCustomSelectionIdentity(*ring, customSelection);
            if (!customIdentity) {
                return LeftSelectionRequest {
                    .ring = ring,
                    .blocked = true,
                };
            }

            return LeftSelectionRequest {
                .ring = ring,
                .customKey = customSelection.key,
                .customIdentity = customIdentity,
                .sourceExtraList = customSelection.extraList,
            };
        }

        return LeftSelectionRequest {
            .ring = ring,
        };
    }

    enum class RingToggleResult {
        kFailed,
        kChanged,
        kHandled,
    };

    [[nodiscard]] bool IsSelectedLeftRing(
        const LeftSelectionRequest& a_request,
        const RingTarget a_target,
        const RE::FormID a_formID
    ) {
        const auto selection = Selection::Get(a_target);
        bool selected = false;
        if (a_request.customKey) {
            selected = selection.MatchesCustomEnchantment(a_formID, *a_request.customKey, a_request.customIdentity);
        } else {
            selected = selection.MatchesForm(a_formID);
        }

        return selected;
    }

    [[nodiscard]] RingToggleResult MoveVanillaRingSlotFormToVirtual(
        const RingTarget a_target,
        RE::TESObjectARMO& a_ring,
        const SelectionOrigin a_origin
    ) {
        if (a_origin == SelectionOrigin::kInventoryMenu) {
            if (auto* inventoryMenu = GetInventoryMenu()) {
                DeselectInventoryItem(*inventoryMenu);
            }

            const auto result = Selection::MoveVanillaRingSlotFormToVirtual(a_ring.GetFormID(), a_target);
            if (result.inventoryChanged) {
                RefreshInventoryMenuAfterVanillaRingSlotMove();
            } else if (result.selectionChanged) {
                RefreshRingRows();
            }

            return result.ChangedState() ? RingToggleResult::kHandled : RingToggleResult::kFailed;
        }

        Selection::QueueVanillaRingSlotFormToVirtual(a_ring.GetFormID(), a_target);
        return RingToggleResult::kHandled;
    }

    [[nodiscard]] RingToggleResult MoveVanillaRingSlotCustomToVirtual(
        const RingTarget a_target,
        RE::TESObjectARMO& a_ring,
        const Inventory::CustomEnchantmentKey& a_customKey,
        const std::optional<Inventory::ExtraListIdentity>& a_customIdentity,
        const SelectionOrigin a_origin
    ) {
        if (a_origin == SelectionOrigin::kInventoryMenu) {
            if (auto* inventoryMenu = GetInventoryMenu()) {
                DeselectInventoryItem(*inventoryMenu);
            }

            const auto result = Selection::MoveVanillaRingSlotCustomToVirtual(
                a_ring.GetFormID(),
                a_customKey,
                a_customIdentity,
                a_target
            );
            if (result.inventoryChanged) {
                RefreshInventoryMenuAfterVanillaRingSlotMove();
            } else if (result.selectionChanged) {
                RefreshRingRows();
            }

            return result.ChangedState() ? RingToggleResult::kHandled : RingToggleResult::kFailed;
        }

        Selection::QueueVanillaRingSlotCustomToVirtual(a_ring.GetFormID(), a_customKey, a_customIdentity, a_target);
        return RingToggleResult::kHandled;
    }

    [[nodiscard]] RingToggleResult SelectCustomRing(
        RE::PlayerCharacter& a_player,
        const RingTarget a_target,
        RE::TESObjectARMO& a_ring,
        const LeftSelectionRequest& a_request,
        const Inventory::CustomEnchantmentKey& a_customKey,
        const SelectionOrigin a_origin
    ) {
        const auto
            sourceMatches = Inventory::FindSourceMatches(a_player, a_ring, a_customKey, a_request.customIdentity);
        auto* sourceExtraList = sourceMatches.firstExtraList;
        if (a_request.sourceExtraList
            && Inventory::MatchesCustomSelection(a_request.sourceExtraList, a_customKey, a_request.customIdentity)) {
            sourceExtraList = a_request.sourceExtraList;
        }
        if (!sourceExtraList || !sourceMatches.HasMatch()) {
            return RingToggleResult::kFailed;
        }

        if (!Inventory::MatchesCustomSelection(sourceExtraList, a_customKey, a_request.customIdentity)) {
            return RingToggleResult::kFailed;
        }

        if (sourceMatches.rightWornExtraList && !sourceMatches.CanWearSameKeyInBothHands()) {
            return MoveVanillaRingSlotCustomToVirtual(
                a_target,
                a_ring,
                a_customKey,
                a_request.customIdentity,
                a_origin
            );
        }

        const auto changed = Selection::SetCustom(a_ring, a_customKey, a_request.customIdentity, a_target);
        return changed ? RingToggleResult::kChanged : RingToggleResult::kFailed;
    }

    [[nodiscard]] RingToggleResult SelectFormRing(
        RE::PlayerCharacter& a_player,
        const RingTarget a_target,
        RE::TESObjectARMO& a_ring,
        const SelectionOrigin a_origin
    ) {
        const auto sourceMatches = Inventory::FindFormOnlyMatches(a_player, a_ring);
        if (!sourceMatches.HasMatch()) {
            RefreshItemRowsForRing(a_player, std::addressof(a_ring));
            return RingToggleResult::kHandled;
        }

        if (sourceMatches.rightWorn && !sourceMatches.CanWearSameFormInBothHands()) {
            return MoveVanillaRingSlotFormToVirtual(a_target, a_ring, a_origin);
        }

        const auto changed = Selection::Set(std::addressof(a_ring), a_target);
        return changed ? RingToggleResult::kChanged : RingToggleResult::kFailed;
    }

    bool ToggleRingForLeftHand(const LeftSelectionRequest& a_request, const SelectionOrigin a_origin) {
        auto* ring = a_request.ring;
        if (!ring || a_request.blocked) {
            return false;
        }

        const auto formID = ring->GetFormID();
        constexpr auto target = kDefaultLeftEquipTarget;
        auto sound = RingSounds::Event::kNone;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (IsSelectedLeftRing(a_request, target, formID)) {
            Selection::Clear(target);
            sound = RingSounds::Event::kUnequip;
        } else {
            if (!player) {
                return false;
            }

            auto result = RingToggleResult::kFailed;
            if (a_request.customKey) {
                result = SelectCustomRing(*player, target, *ring, a_request, *a_request.customKey, a_origin);
            } else {
                result = SelectFormRing(*player, target, *ring, a_origin);
            }
            if (result == RingToggleResult::kFailed) {
                return false;
            }

            if (result == RingToggleResult::kHandled) {
                return true;
            }
            sound = RingSounds::Event::kEquip;
        }

        VirtualRings::RequestRefresh(
            VirtualRings::RefreshOptions {
                .soundTarget = target,
                .sound = sound,
            }
        );
        RefreshRingRows();
        return true;
    }

    class MenuEventSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        static MenuEventSink* GetSingleton() {
            static MenuEventSink sink;
            return std::addressof(sink);
        }

        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent* a_event,
            [[maybe_unused]] RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource
        ) override {
            if (!a_event) {
                return RE::BSEventNotifyControl::kContinue;
            }

            if (a_event->menuName == RE::InventoryMenu::MENU_NAME.data()) {
                if (a_event->opening) {
                    QueueInventoryRefresh();
                }
                return RE::BSEventNotifyControl::kContinue;
            }

            if (a_event->menuName == RE::FavoritesMenu::MENU_NAME.data()) {
                if (a_event->opening) {
                    RefreshFavoritesRows();
                }
            }

            return RE::BSEventNotifyControl::kContinue;
        }
    };

}

void InstallMenuEventSink() {
    if (auto* ui = RE::UI::GetSingleton()) {
        ui->AddEventSink(MenuEventSink::GetSingleton());
        logger::info("UI: menu event sink installed");
    } else {
        logger::warn("UI: menu event sink skipped | reason=noUI");
    }
}

void RegisterInventoryData() {
    if (const auto* scaleform = SKSE::GetScaleformInterface()) {
        scaleform->Register(InventoryDataCallback);
        logger::info("UI: inventory data callback registered");
    } else {
        logger::warn("UI: inventory data callback skipped | reason=noScaleform");
    }
}

bool SelectEntryForLeftHand(RE::InventoryEntryData* a_entry, const SelectionOrigin a_origin) {
    if (!a_entry) {
        return false;
    }

    auto request = BuildSelectionFromEntry(*a_entry);
    if (!request) {
        return false;
    }

    if (request->blocked) {
        return true;
    }

    return ToggleRingForLeftHand(*request, a_origin);
}

void RefreshRingRows() {
    QueueInventoryRefresh();
    RefreshFavoritesRows();
}

void RefreshFavoritesRows() {
    stl::add_ui_task([] {
        static_cast<void>(RestampFavoritesRowsFromEntryData());
    });
}

void RefreshInventoryMenuAfterVanillaRingSlotMove() {
    auto* inventoryMenu = GetInventoryMenu();
    if (!inventoryMenu) {
        return;
    }

    DeselectInventoryItem(*inventoryMenu);
    if (auto* itemList = inventoryMenu->GetRuntimeData().itemList) {
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            itemList->Update(player);
        }
    }
    DeselectInventoryItem(*inventoryMenu);
    static_cast<void>(InvalidateInventoryListData(*inventoryMenu));
    RefreshFavoritesRows();
}

void RefreshItemRowsForRing(RE::Actor& a_actor, const RE::TESObjectARMO* a_ring) {
    RE::SendUIMessage::SendInventoryUpdateMessage(std::addressof(a_actor), a_ring);
    RefreshFavoritesRows();
}

void QueueRefreshAfterRingEquip() {
    stl::add_task([] {
        VirtualRings::RequestRefresh();
        RefreshFavoritesRows();
    });
}
}
