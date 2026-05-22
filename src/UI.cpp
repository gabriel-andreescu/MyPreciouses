#include "UI.h"

#include "BondOfMatrimony.h"
#include "ClonedEquipment.h"
#include "Inventory.h"
#include "Selection.h"
#include "Settings.h"

#include <RE/S/SendUIMessage.h>

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
    constexpr auto kScaleformCustomBlockReason = "lhrsCustomBlockReason";
    constexpr auto kScaleformRightEquipped = "lhrsRightEquipped";
    constexpr auto kInventoryEntryListPath = "_root.Menu_mc.inventoryLists.itemList.entryList";
    constexpr auto kInventoryInvalidateListDataPath = "_root.Menu_mc.inventoryLists.InvalidateListData";

    struct RowSelection {
        Selection::Kind kind {Selection::Kind::kNone};
        std::optional<Inventory::CustomEnchantmentKey> customKey;
    };

    struct LeftSelectionRequest {
        RE::TESObjectARMO* ring {nullptr};
        std::optional<Inventory::CustomEnchantmentKey> customKey;
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

    [[nodiscard]] DisplaySlot GetSelectionChannelForRing(const RE::TESObjectARMO& a_ring) {
        if (Settings::GetSingleton()->IsBondOfMatrimonyEnabled() && BondOfMatrimony::IsBond(std::addressof(a_ring))) {
            return DisplaySlot::kBond;
        }

        return DisplaySlot::kRegular;
    }

    [[nodiscard]] bool IsLeftEquipped(const RE::TESObjectARMO& a_ring, const RowSelection& a_rowSelection) {
        const auto selection = Selection::Get(GetSelectionChannelForRing(a_ring));
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
               && selection.MatchesCustomEnchantment(formID, *a_rowSelection.customKey);
    }

    [[nodiscard]] int GetRingEquipState(
        const RE::TESObjectARMO& a_ring,
        const RowSelection& a_rowSelection,
        const bool a_rightEquipped
    ) {
        const auto leftEquipped = IsLeftEquipped(a_ring, a_rowSelection);

        if (leftEquipped && a_rightEquipped) {
            return kSkyUIEquipStateBoth;
        }

        if (leftEquipped) {
            return kSkyUIEquipStateLeft;
        }

        if (a_rightEquipped) {
            return kSkyUIEquipStateRight;
        }

        return kSkyUIEquipStateNone;
    }

    [[nodiscard]] std::optional<bool> GetLiveRightEquipped(
        RE::TESObjectARMO& a_ring,
        const RowSelection& a_rowSelection
    ) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return std::nullopt;
        }

        if (a_rowSelection.kind == Selection::Kind::kCustomEnchantment && a_rowSelection.customKey) {
            const auto matches = Inventory::FindSourceMatches(*player, a_ring, *a_rowSelection.customKey);
            return matches.rightWornExtraList != nullptr;
        }

        if (a_rowSelection.kind != Selection::Kind::kFormOnly) {
            return std::nullopt;
        }

        auto* entry = Inventory::FindEntry(*player, a_ring);
        if (!entry) {
            return std::nullopt;
        }

        if (!entry->extraLists) {
            return entry->IsWorn(false);
        }

        for (auto* extraList : *entry->extraLists) {
            if (Inventory::IsRightWorn(extraList) && !Inventory::HasCustomEnchantment(extraList)) {
                return true;
            }
        }

        return false;
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

        return RowSelection {
            .kind = Selection::Kind::kCustomEnchantment,
            .customKey = Inventory::CustomEnchantmentKey {
                .enchantmentFormID = static_cast<RE::FormID>(enchantmentID.GetNumber()),
                .charge = static_cast<std::uint16_t>(charge.GetNumber()),
                .removeOnUnequip = removeOnUnequip.GetBool(),
                .playerDisplayName = GetScaleformStringMember(a_object, kScaleformCustomDisplayName),
            },
        };
    }

    void StampScaleformSelection(
        RE::GFxValue& a_object,
        const Selection::Kind a_kind,
        const std::optional<Inventory::CustomEnchantmentKey>& a_customKey,
        const Inventory::EntryCustomFailure a_failure
    ) {
        a_object.SetMember(kScaleformSelectionKind, std::to_underlying(a_kind));
        a_object.SetMember(kScaleformCustomEnchantmentID, a_customKey ? a_customKey->enchantmentFormID : 0);
        a_object.SetMember(kScaleformCustomCharge, a_customKey ? a_customKey->charge : 0);
        a_object.SetMember(kScaleformCustomRemoveOnUnequip, a_customKey && a_customKey->removeOnUnequip);

        RE::GFxValue displayName;
        displayName.SetString(a_customKey ? std::string_view {a_customKey->playerDisplayName} : std::string_view {});
        a_object.SetMember(kScaleformCustomDisplayName, displayName);
        a_object.SetMember(kScaleformCustomBlockReason, std::to_underlying(a_failure));
    }

    [[nodiscard]] std::optional<bool> RestampRingObjectFromStoredSelection(RE::GFxValue& a_object) {
        const auto formID = GetScaleformFormID(a_object);
        if (!formID) {
            return std::nullopt;
        }

        auto* ring = Inventory::AsRing(RE::TESForm::LookupByID<RE::TESObjectARMO>(*formID));
        if (!ring) {
            return std::nullopt;
        }

        const auto previousEquipState = GetScaleformEquipState(a_object);
        const auto rowSelection = GetScaleformRowSelection(a_object);
        const auto previousRightEquipped = previousEquipState
                                           == kSkyUIEquipStateRight
                                           || previousEquipState
                                           == kSkyUIEquipStateBoth;
        const auto storedRightEquipped = GetScaleformBoolMember(a_object, kScaleformRightEquipped);
        const auto liveRightEquipped = GetLiveRightEquipped(*ring, rowSelection);
        const auto rowRightEquipped = liveRightEquipped.value_or(storedRightEquipped.value_or(previousRightEquipped));
        const auto equipState = GetRingEquipState(*ring, rowSelection, rowRightEquipped);
        a_object.SetMember(kScaleformRightEquipped, rowRightEquipped);
        a_object.SetMember("equipState", equipState);
        a_object.SetMember("isEquipped", equipState > kSkyUIEquipStateNone);

        return previousEquipState != equipState;
    }

    [[nodiscard]] std::optional<bool> StampEntryRingObject(RE::GFxValue& a_object, RE::InventoryEntryData& a_entry) {
        auto* ring = Inventory::AsRing(a_entry.GetObject());
        if (!ring) {
            return std::nullopt;
        }

        auto customSelection = Inventory::ResolveCustomSelection(a_entry);
        auto selectionKind = Selection::Kind::kFormOnly;
        std::optional<Inventory::CustomEnchantmentKey> customKey;
        auto rightEquipped = a_entry.IsWorn(false);

        if (customSelection.failure != Inventory::EntryCustomFailure::kNone) {
            selectionKind = Selection::Kind::kNone;
        } else if (customSelection.HasCustomEnchantment()) {
            selectionKind = Selection::Kind::kCustomEnchantment;
            customKey = customSelection.key;
            rightEquipped = Inventory::IsRightWorn(customSelection.extraList);
        }

        StampScaleformSelection(a_object, selectionKind, customKey, customSelection.failure);

        const auto equipState = GetRingEquipState(
            *ring,
            RowSelection {
                .kind = selectionKind,
                .customKey = customKey,
            },
            rightEquipped
        );
        const auto previousEquipState = GetScaleformEquipState(a_object);
        a_object.SetMember(kScaleformRightEquipped, rightEquipped);
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

        auto* movie = inventoryMenu->uiMovie.get();
        if (!movie) {
            return false;
        }

        RE::GFxValue entryList;
        if (!movie->GetVariable(std::addressof(entryList), kInventoryEntryListPath)) {
            return false;
        }

        if (!entryList.IsArray()) {
            return false;
        }

        const auto entryCount = entryList.GetArraySize();
        std::uint32_t changedEntryRows = 0;

        for (std::uint32_t index = 0; index < entryCount; ++index) {
            RE::GFxValue entryObject;
            if (!entryList.GetElement(index, std::addressof(entryObject))
                || (!entryObject.IsObject() && !entryObject.IsDisplayObject())) {
                continue;
            }

            const auto changed = RestampRingObjectFromStoredSelection(entryObject);
            if (!changed) {
                continue;
            }

            if (!*changed) {
                continue;
            }

            ++changedEntryRows;
            static_cast<void>(entryList.SetElement(index, entryObject));
        }

        if (changedEntryRows == 0) {
            return false;
        }

        return InvalidateInventoryListData(*inventoryMenu);
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

        const auto redrawn = RedrawFavoritesRows(itemList);
        return redrawn;
    }

    void QueueInventoryRefresh() {
        stl::add_ui_task([] {
            static_cast<void>(RestampInventoryRowsFromLiveState());
        });
    }

    void QueueFavoritesRefresh() {
        stl::add_ui_task([] {
            static_cast<void>(RestampFavoritesRowsFromEntryData());
        });
    }

    void QueueEquipStateRefresh() {
        QueueInventoryRefresh();
        QueueFavoritesRefresh();
    }

    [[nodiscard]] std::optional<LeftSelectionRequest> BuildSelectionFromEntry(RE::InventoryEntryData& a_entry) {
        auto* ring = Inventory::AsRing(a_entry.GetObject());
        if (!ring) {
            return std::nullopt;
        }

        auto customSelection = Inventory::ResolveCustomSelection(a_entry);
        if (customSelection.failure != Inventory::EntryCustomFailure::kNone) {
            return LeftSelectionRequest {
                .ring = ring,
                .blocked = true,
            };
        }

        if (customSelection.HasCustomEnchantment()) {
            return LeftSelectionRequest {
                .ring = ring,
                .customKey = customSelection.key,
                .sourceExtraList = customSelection.extraList,
            };
        }

        return LeftSelectionRequest {
            .ring = ring,
        };
    }

    enum class ToggleResult {
        kFailed,
        kChanged,
        kQueued,
    };

    [[nodiscard]] bool IsSelectedLeftRing(
        const LeftSelectionRequest& a_request,
        const DisplaySlot a_channel,
        const RE::FormID a_formID
    ) {
        const auto selection = Selection::Get(a_channel);
        if (a_request.customKey) {
            return selection.MatchesCustomEnchantment(a_formID, *a_request.customKey);
        }

        return selection.MatchesForm(a_formID);
    }

    [[nodiscard]] ToggleResult SelectCustomRing(
        RE::PlayerCharacter& a_player,
        const DisplaySlot a_channel,
        RE::TESObjectARMO& a_ring,
        const LeftSelectionRequest& a_request,
        const Inventory::CustomEnchantmentKey& a_customKey
    ) {
        const auto sourceMatches = Inventory::FindSourceMatches(a_player, a_ring, a_customKey);
        auto* sourceExtraList = sourceMatches.firstExtraList;
        if (a_request.sourceExtraList
            && Inventory::MatchesCustomEnchantmentKey(a_request.sourceExtraList, a_customKey)) {
            sourceExtraList = a_request.sourceExtraList;
        }
        if (!sourceExtraList || !sourceMatches.HasMatch()) {
            return ToggleResult::kFailed;
        }

        if (!Inventory::MatchesCustomEnchantmentKey(sourceExtraList, a_customKey)) {
            return ToggleResult::kFailed;
        }

        if (sourceMatches.rightWornExtraList && !sourceMatches.CanWearSameKeyInBothHands()) {
            Selection::RequestCustomMove(a_ring.GetFormID(), a_customKey, a_channel, true);
            return ToggleResult::kQueued;
        }

        Selection::SetCustom(a_ring, a_customKey, a_channel);
        return ToggleResult::kChanged;
    }

    [[nodiscard]] ToggleResult SelectFormRing(
        RE::PlayerCharacter& a_player,
        const DisplaySlot a_channel,
        RE::TESObjectARMO& a_ring
    ) {
        const auto sourceState = Inventory::GetSourceState(a_player, a_ring);
        if (sourceState.rightWorn && !sourceState.CanWearSameFormInBothHands()) {
            Selection::RequestMove(a_ring.GetFormID(), a_channel, true);
            return ToggleResult::kQueued;
        }

        Selection::Set(std::addressof(a_ring), a_channel);
        return ToggleResult::kChanged;
    }

    bool ToggleRingForLeftHand(const LeftSelectionRequest& a_request) {
        auto* ring = a_request.ring;
        if (!ring || a_request.blocked) {
            return false;
        }

        const auto formID = ring->GetFormID();
        const auto channel = GetSelectionChannelForRing(*ring);
        if (IsSelectedLeftRing(a_request, channel, formID)) {
            Selection::Clear(channel);
        } else {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                return false;
            }

            auto result = ToggleResult::kFailed;
            if (a_request.customKey) {
                result = SelectCustomRing(*player, channel, *ring, a_request, *a_request.customKey);
            } else {
                result = SelectFormRing(*player, channel, *ring);
            }
            if (result == ToggleResult::kFailed) {
                return false;
            }

            if (result == ToggleResult::kQueued) {
                return true;
            }
        }

        ClonedEquipment::RequestRefreshWithSounds(channel);
        QueueEquipStateRefresh();
        return true;
    }

    bool SelectEntryForLeftHandImpl(RE::InventoryEntryData* a_entry) {
        if (!a_entry) {
            return false;
        }

        auto* ring = Inventory::AsRing(a_entry->GetObject());
        if (!ring) {
            return false;
        }

        auto request = BuildSelectionFromEntry(*a_entry);
        if (!request) {
            return false;
        }

        if (request->blocked) {
            return true;
        }

        return ToggleRingForLeftHand(*request);
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
                    QueueFavoritesRefresh();
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

bool SelectEntryForLeftHand(RE::InventoryEntryData* a_entry) {
    return SelectEntryForLeftHandImpl(a_entry);
}

void RefreshRows() {
    QueueEquipStateRefresh();
}

void RefreshEquipmentSoon(const RE::FormID a_ringFormID) {
    stl::add_task([a_ringFormID] {
        ClonedEquipment::RequestRefresh();
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            auto* ring = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_ringFormID);
            RE::SendUIMessage::SendInventoryUpdateMessage(player, ring);
            RefreshRows();
        }
    });
}
}
