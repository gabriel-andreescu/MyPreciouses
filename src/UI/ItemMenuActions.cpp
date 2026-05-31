#include "UI/ItemMenuActions.h"

#include "Equipment/AssignmentActions.h"
#include "Equipment/AssignmentStore.h"
#include "Inventory.h"
#include "Localization.h"
#include "Settings.h"
#include "SourceModelFootprints.h"
#include "UI/FingerSelectMenu.h"
#include "UI/RingItemRows.h"

#include <RE/B/BSInputDevice.h>
#include <RE/B/BSPCGamepadDeviceDelegate.h>
#include <RE/B/BSPCGamepadDeviceHandler.h>
#include <RE/B/BSWin32KeyboardDevice.h>
#include <RE/B/BSWin32MouseDevice.h>
#include <RE/S/SendHUDMessage.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <SKSE/InputMap.h>

namespace UI::ItemMenuActions {
namespace {
    constexpr auto kFingerSelectTitleKey = "$LHRS_FingerSelect_Title";
    constexpr auto kFingerSelectFingerHeaderKey = "$LHRS_FingerSelect_FingerHeader";
    constexpr auto kFingerSelectEquippedHeaderKey = "$LHRS_FingerSelect_EquippedHeader";
    constexpr auto kFingerSelectEquipActionKey = "$LHRS_FingerSelect_Action_Equip";
    constexpr auto kFingerSelectUnequipActionKey = "$LHRS_FingerSelect_Action_Unequip";
    constexpr auto kFingerSelectReplaceActionKey = "$LHRS_FingerSelect_Action_Replace";
    constexpr auto kFingerSelectWontFitActionKey = "$LHRS_FingerSelect_Action_WontFit";
    constexpr auto kFingerSelectCancelActionKey = "$LHRS_FingerSelect_Action_Cancel";
    constexpr auto kFingerPlaceholder = "{FINGER}"sv;
    constexpr auto kRingWontFitFingerMessageKey = "$LHRS_Message_RingWontFitFinger";
    constexpr auto kRingUnequipBlockedMessageKey = "$LHRS_Message_RingUnequipBlocked";

    struct MenuRingSource {
        ItemMenuHost hostMenu {ItemMenuHost::kInventory};
        Core::ActorKey itemActor;
        RE::TESObjectARMO* ring {nullptr};
        Core::ItemSource itemSource;
        RE::ExtraDataList* sourceExtraList {nullptr};
        bool blocked {false};
    };

    struct StoredMenuRingSource {
        ItemMenuHost hostMenu {ItemMenuHost::kInventory};
        Core::ActorKey itemActor;
        Core::ItemSource itemSource;
        Core::FingerMask sourceFingerMask;
        std::optional<Core::Target> moveSourceTarget;
    };

    struct FingerSelectTrigger {
        bool requested {false};
        RE::INPUT_DEVICE inputDevice {RE::INPUT_DEVICE::kKeyboard};
    };

    [[nodiscard]] FingerSelectMenu::Labels GetFingerSelectLabels() {
        return FingerSelectMenu::Labels {
            Localization::Translate(kFingerSelectTitleKey, "ASSIGN RING"),
            Localization::Translate(kFingerSelectFingerHeaderKey, "FINGER"),
            Localization::Translate(kFingerSelectEquippedHeaderKey, "EQUIPPED RING"),
            Localization::Translate(kFingerSelectEquipActionKey, "Equip"),
            Localization::Translate(kFingerSelectUnequipActionKey, "Unequip"),
            Localization::Translate(kFingerSelectReplaceActionKey, "Replace"),
            Localization::Translate(kFingerSelectWontFitActionKey, "Won't fit"),
            Localization::Translate(kFingerSelectCancelActionKey, "Cancel"),
        };
    }

    void ReplaceToken(std::string& a_text, const std::string_view a_token, const std::string_view a_value) {
        const auto token = std::string(a_token);
        const auto value = std::string(a_value);
        auto pos = std::string::size_type {0};
        while ((pos = a_text.find(token, pos)) != std::string::npos) {
            a_text.replace(pos, token.size(), value);
            pos += value.size();
        }
    }

    void ShowWontFitMessage(const Core::Target a_target) {
        auto message = Localization::Translate(
            kRingWontFitFingerMessageKey,
            "This ring won't fit starting from {FINGER}."
        );
        ReplaceToken(message, kFingerPlaceholder, Localization::TranslateFingerLabel(a_target.finger));
        RE::SendHUDMessage::ShowHUDMessage(message.c_str(), nullptr, true);
    }

    void ShowRingUnequipBlockedMessage() {
        const auto message = Localization::Translate(kRingUnequipBlockedMessageKey, "This ring cannot be unequipped.");
        RE::SendHUDMessage::ShowHUDMessage(message.c_str(), nullptr, true);
    }

    [[nodiscard]] std::optional<MenuRingSource> ResolveMenuRingSource(
        RE::InventoryEntryData& a_entry,
        const ItemMenuHost a_hostMenu,
        const Core::ActorKey a_itemActor
    ) {
        auto* actor = Core::ResolveActor(a_itemActor);
        if (!actor) {
            return std::nullopt;
        }

        auto source = Inventory::ResolveEntryRingSource(*actor, a_entry);
        if (!source) {
            return std::nullopt;
        }

        return MenuRingSource {
            .hostMenu = a_hostMenu,
            .itemActor = a_itemActor,
            .ring = source->ring,
            .itemSource = source->source,
            .sourceExtraList = source->sourceExtraList,
            .blocked = source->ring == nullptr || !source->source.IsAssigned(),
        };
    }

    [[nodiscard]] Equipment::SourceSelection ToEquipmentSourceSelection(const MenuRingSource& a_source) {
        return Equipment::SourceSelection {
            .actor = a_source.itemActor,
            .itemSource = a_source.itemSource,
            .preferredExtraList = a_source.sourceExtraList,
        };
    }

    void ApplyEquipmentActionResult(
        const ItemMenuHost a_hostMenu,
        const Core::ActorKey a_actor,
        const RE::FormID a_sourceFormID,
        const Equipment::ActionResult a_result
    ) {
        if (a_result.blockReason == Equipment::ActionBlockReason::kRightHandRingCannotBeUnequipped) {
            ShowRingUnequipBlockedMessage();
        }

        ::UI::RefreshItemRowsAfterEquipmentAction(a_hostMenu, a_actor, a_sourceFormID, a_result);
    }

    bool ToggleMenuRingForTarget(
        const MenuRingSource& a_source,
        const Core::Target a_target,
        const std::optional<Core::Target> a_moveSourceTarget = std::nullopt
    ) {
        auto* ring = a_source.ring;
        if (!ring || a_source.blocked) {
            return false;
        }

        const auto sourceFormID = ring->GetFormID();
        auto sourceSelection = ToEquipmentSourceSelection(a_source);
        auto queueMode = a_source.hostMenu == ItemMenuHost::kFavorites ? Equipment::QueueMode::kQueued
                                                                       : Equipment::QueueMode::kImmediate;
        auto result = Equipment::ToggleTarget(
            sourceSelection,
            a_target,
            a_moveSourceTarget,
            queueMode,
            [hostMenu = a_source.hostMenu, actor = a_source.itemActor, sourceFormID](const auto queuedResult) {
                ApplyEquipmentActionResult(hostMenu, actor, sourceFormID, queuedResult);
            }
        );
        ApplyEquipmentActionResult(a_source.hostMenu, a_source.itemActor, sourceFormID, result);
        return result.WasHandled();
    }

    [[nodiscard]] Core::Target DefaultTargetForHand(const Core::Hand a_hand) {
        return a_hand == Core::Hand::kLeft ? Core::kDefaultLeftTarget : Core::kVanillaRingSlotTarget;
    }

    [[nodiscard]] StoredMenuRingSource StoreMenuRingSource(
        const MenuRingSource& a_source,
        const std::optional<Core::Target> a_moveSourceTarget
    ) {
        return StoredMenuRingSource {
            .hostMenu = a_source.hostMenu,
            .itemActor = a_source.itemActor,
            .itemSource = a_source.itemSource,
            .sourceFingerMask = a_source.ring ? SourceModelFootprints::GetSourceFingerMask(*a_source.ring)
                                              : Core::FingerMask {},
            .moveSourceTarget = a_moveSourceTarget,
        };
    }

    [[nodiscard]] std::optional<MenuRingSource> RestoreMenuRingSource(const StoredMenuRingSource& a_source) {
        auto* ring = Inventory::AsRing(RE::TESForm::LookupByID<RE::TESObjectARMO>(a_source.itemSource.sourceFormID));
        auto* actor = Core::ResolveActor(a_source.itemActor);
        if (!ring || !actor || Inventory::GetCount(*actor, *ring) <= 0) {
            return std::nullopt;
        }

        if (a_source.itemSource.IsCustomEnchantment()) {
            const auto sourceMatches = Inventory::FindCustomSourceMatches(
                *actor,
                *ring,
                a_source.itemSource.customEnchantment,
                a_source.itemSource.extraUniqueID
            );
            if (!sourceMatches.HasMatch()) {
                return std::nullopt;
            }
        }

        return MenuRingSource {
            .hostMenu = a_source.hostMenu,
            .itemActor = a_source.itemActor,
            .ring = ring,
            .itemSource = a_source.itemSource,
        };
    }

    [[nodiscard]] bool IsSelectedTarget(const MenuRingSource& a_source, const Core::Target a_target) {
        if (!a_source.ring) {
            return false;
        }

        if (a_target == Core::kVanillaRingSlotTarget) {
            return Equipment::IsInVanillaRingSlot(ToEquipmentSourceSelection(a_source));
        }

        return Equipment::IsSelected(ToEquipmentSourceSelection(a_source), a_target);
    }

    [[nodiscard]] bool ShouldOpenFingerSelectorForHand(const MenuRingSource& a_source, const Core::Hand a_hand) {
        const auto selectedTargets = Equipment::CollectSelectedTargetsOnHand(
            ToEquipmentSourceSelection(a_source),
            a_hand
        );
        if (selectedTargets.empty()) {
            return false;
        }

        return selectedTargets.size() > 1 || selectedTargets.front() != DefaultTargetForHand(a_hand);
    }

    [[nodiscard]] std::optional<Core::Target> FindPreferredSelectedTargetOnHand(
        const MenuRingSource& a_source,
        const Core::Hand a_hand
    ) {
        const auto selectedTargets = Equipment::CollectSelectedTargetsOnHand(
            ToEquipmentSourceSelection(a_source),
            a_hand
        );
        const auto nonIndexTarget = std::ranges::find_if(selectedTargets, [](const auto target) {
            return target.finger != Core::Finger::kIndex;
        });
        if (nonIndexTarget != selectedTargets.end()) {
            return *nonIndexTarget;
        }

        if (!selectedTargets.empty()) {
            return selectedTargets.front();
        }

        return std::nullopt;
    }

    [[nodiscard]] std::string GetMenuSourceRingLabel(const MenuRingSource& a_source) {
        if (a_source.itemSource.IsCustomEnchantment()
            && !a_source.itemSource.customEnchantment.playerDisplayName.empty()) {
            return a_source.itemSource.customEnchantment.playerDisplayName;
        }

        return a_source.ring ? RingItemRows::GetRingDisplayName(*a_source.ring) : RingItemRows::kEmptyRingDisplayName;
    }

    [[nodiscard]] std::string GetVirtualTargetRingLabel(const Core::ActorKey a_actor, const Core::Target a_target) {
        const auto selection = Equipment::AssignmentStore::Get(a_actor, a_target);
        if (!selection.IsAssigned()) {
            return RingItemRows::kEmptyRingDisplayName;
        }

        if (selection.source.IsCustomEnchantment() && !selection.source.customEnchantment.playerDisplayName.empty()) {
            return selection.source.customEnchantment.playerDisplayName;
        }

        const auto* ring = Inventory::AsRing(RE::TESForm::LookupByID(selection.source.sourceFormID));
        return ring ? RingItemRows::GetRingDisplayName(*ring) : RingItemRows::kEmptyRingDisplayName;
    }

    [[nodiscard]] std::optional<std::string> GetVirtualOccupyingTargetRingLabel(
        const Core::ActorKey a_actor,
        const Core::Target a_target
    ) {
        const auto snapshot = Equipment::AssignmentStore::GetSnapshot(a_actor);
        for (const auto sourceTarget : Core::kVirtualTargets) {
            const auto& assignment = snapshot.byTarget[Core::ToIndex(sourceTarget)];
            if (!assignment.IsAssigned()) {
                continue;
            }

            const auto* ring = Inventory::AsRing(RE::TESForm::LookupByID(assignment.source.sourceFormID));
            if (!ring || !SourceModelFootprints::GetProjectedTargets(*ring, sourceTarget).Contains(a_target)) {
                continue;
            }

            if (assignment.source.IsCustomEnchantment()
                && !assignment.source.customEnchantment.playerDisplayName.empty()) {
                return assignment.source.customEnchantment.playerDisplayName;
            }

            return RingItemRows::GetRingDisplayName(*ring);
        }

        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> GetRightWornExtraListLabel(
        const RE::InventoryEntryData& a_entry,
        const RE::TESObjectARMO& a_ring
    ) {
        if (!a_entry.extraLists) {
            return std::nullopt;
        }

        for (auto* extraList : *a_entry.extraLists) {
            if (!Inventory::IsRightWorn(extraList)) {
                continue;
            }

            if (extraList) {
                const auto custom = Inventory::ReadCustomEnchantment(*extraList);
                if (custom && custom->playerDisplayName && !custom->playerDisplayName->empty()) {
                    return *custom->playerDisplayName;
                }
            }

            return RingItemRows::GetRingDisplayName(a_ring);
        }

        return std::nullopt;
    }

    struct EquippedRingLabel {
        RE::TESObjectARMO* ring {nullptr};
        std::string label;
    };

    [[nodiscard]] std::optional<EquippedRingLabel> GetRightWornEntry(RE::InventoryEntryData& a_entry) {
        auto* ring = Inventory::AsRing(a_entry.GetObject());
        if (!ring) {
            return std::nullopt;
        }

        if (auto label = GetRightWornExtraListLabel(a_entry, *ring)) {
            return EquippedRingLabel {
                .ring = ring,
                .label = std::move(*label),
            };
        }

        if (!a_entry.extraLists && a_entry.IsWorn(false)) {
            return EquippedRingLabel {
                .ring = ring,
                .label = RingItemRows::GetRingDisplayName(*ring),
            };
        }

        return std::nullopt;
    }

    [[nodiscard]] std::optional<EquippedRingLabel> GetRightWornRing(const Core::ActorKey a_actor) {
        auto* actor = Core::ResolveActor(a_actor);
        auto* inventoryChanges = actor ? actor->GetInventoryChanges() : nullptr;
        if (!inventoryChanges || !inventoryChanges->entryList) {
            return std::nullopt;
        }

        for (auto* entry : *inventoryChanges->entryList) {
            if (entry) {
                if (auto label = GetRightWornEntry(*entry)) {
                    return label;
                }
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> GetRightWornOccupyingTargetRingLabel(
        const Core::ActorKey a_actor,
        const Core::Target a_target
    ) {
        const auto rightWorn = GetRightWornRing(a_actor);
        if (!rightWorn || !rightWorn->ring) {
            return std::nullopt;
        }

        return SourceModelFootprints::GetProjectedTargets(*rightWorn->ring, Core::kVanillaRingSlotTarget)
                       .Contains(a_target)
                   ? std::make_optional(rightWorn->label)
                   : std::nullopt;
    }

    [[nodiscard]] std::string GetEquippedRingLabel(const Core::ActorKey a_actor, const Core::Target a_target) {
        if (a_target == Core::kVanillaRingSlotTarget) {
            if (const auto rightWornLabel = GetRightWornOccupyingTargetRingLabel(a_actor, a_target)) {
                return *rightWornLabel;
            }

            if (const auto virtualLabel = GetVirtualOccupyingTargetRingLabel(a_actor, a_target)) {
                return *virtualLabel;
            }

            return RingItemRows::kEmptyRingDisplayName;
        }

        if (const auto virtualLabel = GetVirtualOccupyingTargetRingLabel(a_actor, a_target)) {
            return *virtualLabel;
        }

        if (const auto rightWornLabel = GetRightWornOccupyingTargetRingLabel(a_actor, a_target)) {
            return *rightWornLabel;
        }

        return GetVirtualTargetRingLabel(a_actor, a_target);
    }

    [[nodiscard]] bool IsFingerTargetOccupied(const Core::ActorKey a_actor, const Core::Target a_target) {
        if (a_target == Core::kVanillaRingSlotTarget) {
            return GetRightWornOccupyingTargetRingLabel(a_actor, a_target).has_value()
                   || GetVirtualOccupyingTargetRingLabel(a_actor, a_target).has_value();
        }

        return Equipment::AssignmentStore::Get(a_actor, a_target).IsAssigned()
               || GetRightWornOccupyingTargetRingLabel(a_actor, a_target).has_value()
               || GetVirtualOccupyingTargetRingLabel(a_actor, a_target).has_value();
    }

    [[nodiscard]] std::optional<Core::Target> ResolveSelectableTarget(
        const Core::FingerMask& a_sourceFingerMask,
        const Core::Target a_target
    ) {
        return SourceModelFootprints::GetProjectedTargets(a_sourceFingerMask, a_target).Empty()
                   ? std::nullopt
                   : std::make_optional(a_target);
    }

    [[nodiscard]] bool WouldUnequipProtectedRightHandRing(
        const Core::ActorKey a_actor,
        const Core::FingerMask& a_sourceFingerMask,
        const Core::Target a_target
    ) {
        const auto projectedTargets = SourceModelFootprints::GetProjectedTargets(a_sourceFingerMask, a_target);
        if (!projectedTargets.Contains(Core::kVanillaRingSlotTarget)) {
            return false;
        }

        auto* actor = Core::ResolveActor(a_actor);
        return actor && Inventory::HasProtectedRightWornRing(*actor);
    }

    [[nodiscard]] bool IsAnyFingerTargetOccupied(const Core::ActorKey a_actor, const Core::TargetMask& a_targets) {
        return std::ranges::any_of(Core::kAllTargets, [&](const auto target) {
            return a_targets.Contains(target) && IsFingerTargetOccupied(a_actor, target);
        });
    }

    [[nodiscard]] std::string GetFingerRowActionLabel(
        const MenuRingSource& a_source,
        const Core::Target a_target,
        const Core::TargetMask& a_occupiedTargets,
        const FingerSelectMenu::Labels& a_labels
    ) {
        if (IsSelectedTarget(a_source, a_target)) {
            return a_labels.unequipAction;
        }

        if (IsAnyFingerTargetOccupied(a_source.itemActor, a_occupiedTargets)) {
            return a_labels.replaceAction;
        }

        return a_labels.equipAction;
    }

    [[nodiscard]] std::array<FingerSelectMenu::Row, FingerSelectMenu::kRowCount> BuildFingerRows(
        const MenuRingSource& a_source,
        const Core::Hand a_hand,
        const FingerSelectMenu::Labels& a_labels,
        const Core::FingerMask& a_sourceFingerMask
    ) {
        static_assert(FingerSelectMenu::kRowCount == Core::kFingers.size());

        std::array<FingerSelectMenu::Row, FingerSelectMenu::kRowCount> rows;
        for (std::size_t index = 0; index < rows.size(); ++index) {
            const auto finger = Core::kFingers[index];
            const auto target = Core::Target {
                .hand = a_hand,
                .finger = finger,
            };
            const auto selectableTarget = ResolveSelectableTarget(a_sourceFingerMask, target);
            const auto occupiedTargets = selectableTarget ? SourceModelFootprints::GetProjectedTargets(
                                                                a_sourceFingerMask,
                                                                *selectableTarget
                                                            )
                                                          : Core::TargetMask {};
            rows[index] = FingerSelectMenu::Row {
                .target = target,
                .fingerLabel = Localization::TranslateFingerLabel(finger),
                .equippedRingLabel = GetEquippedRingLabel(a_source.itemActor, target),
                .actionLabel = selectableTarget
                                   ? GetFingerRowActionLabel(a_source, *selectableTarget, occupiedTargets, a_labels)
                                   : a_labels.wontFitAction,
                .previewTargetBits = occupiedTargets.Bits(),
            };
        }
        return rows;
    }

    [[nodiscard]] std::size_t GetRowIndex(
        const std::array<FingerSelectMenu::Row, FingerSelectMenu::kRowCount>& a_rows,
        const Core::Target a_target
    ) {
        const auto it = std::ranges::find_if(a_rows, [a_target](const auto& a_row) {
            return a_row.target == a_target;
        });
        return it == a_rows.end() ? 0 : static_cast<std::size_t>(std::distance(a_rows.begin(), it));
    }

    [[nodiscard]] std::optional<Core::Target> FindFirstSelectableTargetOnHand(
        const Core::FingerMask& a_sourceFingerMask,
        const Core::Hand a_hand
    ) {
        for (const auto finger : Core::kFingers) {
            const auto target = Core::Target {
                .hand = a_hand,
                .finger = finger,
            };
            if (ResolveSelectableTarget(a_sourceFingerMask, target)) {
                return target;
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] Core::Target FindFingerSelectorStartTarget(
        const MenuRingSource& a_source,
        const Core::Hand a_hand,
        const Core::FingerMask& a_sourceFingerMask
    ) {
        if (const auto selectedTarget = FindPreferredSelectedTargetOnHand(a_source, a_hand)) {
            if (ResolveSelectableTarget(a_sourceFingerMask, *selectedTarget)) {
                return *selectedTarget;
            }
        }

        const auto defaultTarget = DefaultTargetForHand(a_hand);
        if (ResolveSelectableTarget(a_sourceFingerMask, defaultTarget)) {
            return defaultTarget;
        }

        return FindFirstSelectableTargetOnHand(a_sourceFingerMask, a_hand).value_or(defaultTarget);
    }

    [[nodiscard]] std::uint16_t GetPreviewSourceTargetBits(
        const Core::FingerMask& a_sourceFingerMask,
        const std::optional<Core::Target> a_sourceTarget
    ) {
        if (!a_sourceTarget) {
            return 0;
        }

        return SourceModelFootprints::GetProjectedTargets(a_sourceFingerMask, *a_sourceTarget).Bits();
    }

    void ApplyStoredMenuRingSource(const StoredMenuRingSource& a_source, const Core::Target a_target) {
        auto source = RestoreMenuRingSource(a_source);
        if (!source) {
            return;
        }

        ToggleMenuRingForTarget(*source, a_target, a_source.moveSourceTarget);
    }

    bool ShowFingerSelector(
        const MenuRingSource& a_source,
        const Core::Hand a_hand,
        const RE::INPUT_DEVICE a_inputDevice
    ) {
        auto* ring = a_source.ring;
        if (!ring) {
            return false;
        }

        if (Equipment::IsProtectedInVanillaRingSlot(ToEquipmentSourceSelection(a_source))) {
            ShowRingUnequipBlockedMessage();
            return true;
        }

        const auto sourceFingerMask = SourceModelFootprints::GetSourceFingerMask(*ring);

        auto labels = GetFingerSelectLabels();
        auto rows = BuildFingerRows(a_source, a_hand, labels, sourceFingerMask);
        const auto selectedTarget = FindPreferredSelectedTargetOnHand(a_source, a_hand);
        const auto startTarget = FindFingerSelectorStartTarget(a_source, a_hand, sourceFingerMask);
        const auto startIndex = GetRowIndex(rows, startTarget);
        const auto storedSource = StoreMenuRingSource(a_source, selectedTarget);

        return FingerSelectMenu::Show(
            FingerSelectMenu::Data {
                .labels = std::move(labels),
                .ringName = GetMenuSourceRingLabel(a_source),
                .rows = std::move(rows),
                .previewEmptyRingLabel = RingItemRows::kEmptyRingDisplayName,
                .previewSourceTargetBits = GetPreviewSourceTargetBits(sourceFingerMask, selectedTarget),
                .selectedIndex = startIndex,
                .inputDevice = a_inputDevice,
                .hostMenu = a_source.hostMenu,
                .onResult = [storedSource](const FingerSelectMenu::Result a_result) {
                    if (a_result.action == FingerSelectMenu::Result::Action::kCancel || !a_result.target) {
                        return FingerSelectMenu::ResultDisposition::kClose;
                    }

                    const auto target = ResolveSelectableTarget(storedSource.sourceFingerMask, *a_result.target);
                    if (!target) {
                        ShowWontFitMessage(*a_result.target);
                        return FingerSelectMenu::ResultDisposition::kKeepOpen;
                    }

                    if (WouldUnequipProtectedRightHandRing(
                            storedSource.itemActor,
                            storedSource.sourceFingerMask,
                            *target
                        )) {
                        ShowRingUnequipBlockedMessage();
                        return FingerSelectMenu::ResultDisposition::kKeepOpen;
                    }

                    stl::add_task([storedSource, target = *target] {
                        ApplyStoredMenuRingSource(storedSource, target);
                    });
                    return FingerSelectMenu::ResultDisposition::kClose;
                },
            }
        );
    }

    [[nodiscard]] RE::BSWin32KeyboardDevice* GetKeyboard(RE::BSInputDeviceManager& a_input) {
        auto* device = a_input.devices[std::to_underlying(RE::INPUT_DEVICE::kKeyboard)];
        return device ? stl::unrestricted_cast<RE::BSWin32KeyboardDevice*>(device) : nullptr;
    }

    [[nodiscard]] bool IsKeyboardKeyPressed(const RE::BSWin32KeyboardDevice& a_keyboard, const std::uint32_t a_key) {
        const auto& keys = a_keyboard.GetRuntimeData().curState;
        return a_key < std::size(keys) && (keys[a_key] & 0x80) != 0;
    }

    [[nodiscard]] RE::BSWin32MouseDevice* GetMouse(RE::BSInputDeviceManager& a_input) {
        auto* device = a_input.devices[std::to_underlying(RE::INPUT_DEVICE::kMouse)];
        return device ? stl::unrestricted_cast<RE::BSWin32MouseDevice*>(device) : nullptr;
    }

    [[nodiscard]] RE::BSPCGamepadDeviceDelegate* GetGamepad(RE::BSInputDeviceManager& a_input) {
        auto* device = a_input.devices[std::to_underlying(RE::INPUT_DEVICE::kGamepad)];
        auto* handler = device ? stl::unrestricted_cast<RE::BSPCGamepadDeviceHandler*>(device) : nullptr;
        return handler ? handler->GetRuntimeData().currentPCGamePadDelegate : nullptr;
    }

    [[nodiscard]] bool IsDeviceKeyPressed(const RE::BSInputDevice& a_device, const std::uint32_t a_key) {
        const auto& buttons = a_device.GetRuntimeData().deviceButtons;
        const auto it = buttons.find(a_key);
        return it != buttons.end() && it->second && it->second->heldDownSecs > 0.0F;
    }

    [[nodiscard]] bool IsPCFingerSelectModifierDown() {
        auto* input = RE::BSInputDeviceManager::GetSingleton();
        if (!input) {
            return false;
        }

        const auto keyCode = Settings::GetSingleton()->GetFingerSelectModifierKey();
        if (keyCode < SKSE::InputMap::kMacro_MouseButtonOffset) {
            auto* keyboard = GetKeyboard(*input);
            return keyboard && IsKeyboardKeyPressed(*keyboard, keyCode);
        }

        auto* mouse = GetMouse(*input);
        return mouse
               && IsDeviceKeyPressed(
                   static_cast<const RE::BSInputDevice&>(*mouse),
                   keyCode - SKSE::InputMap::kMacro_MouseButtonOffset
               );
    }

    [[nodiscard]] bool IsGamepadFingerSelectModifierDown() {
        auto* input = RE::BSInputDeviceManager::GetSingleton();
        auto* gamepad = input ? GetGamepad(*input) : nullptr;
        if (!gamepad) {
            return false;
        }

        const auto key = SKSE::InputMap::GamepadKeycodeToMask(
            Settings::GetSingleton()->GetFingerSelectModifierButton()
        );
        return key != 0xFF && IsDeviceKeyPressed(static_cast<const RE::BSInputDevice&>(*gamepad), key);
    }

    [[nodiscard]] RE::INPUT_DEVICE GetPreferredInputDevice() {
        auto* input = RE::BSInputDeviceManager::GetSingleton();
        return input && GetGamepad(*input) ? RE::INPUT_DEVICE::kGamepad : RE::INPUT_DEVICE::kKeyboard;
    }

    [[nodiscard]] FingerSelectTrigger GetFingerSelectTrigger() {
        if (Settings::GetSingleton()->AlwaysChooseFinger()) {
            return FingerSelectTrigger {
                .requested = true,
                .inputDevice = GetPreferredInputDevice(),
            };
        }

        if (IsPCFingerSelectModifierDown()) {
            return FingerSelectTrigger {
                .requested = true,
                .inputDevice = RE::INPUT_DEVICE::kKeyboard,
            };
        }

        if (IsGamepadFingerSelectModifierDown()) {
            return FingerSelectTrigger {
                .requested = true,
                .inputDevice = RE::INPUT_DEVICE::kGamepad,
            };
        }

        return {};
    }
}

bool HandleRingUseFromMenuEntry(
    RE::InventoryEntryData* a_entry,
    const Core::Hand a_hand,
    const ItemMenuHost a_hostMenu,
    const Core::ActorKey a_itemActor
) {
    if (FingerSelectMenu::IsPendingOrOpen()) {
        return true;
    }

    if (!a_entry) {
        return false;
    }

    auto source = ResolveMenuRingSource(*a_entry, a_hostMenu, a_itemActor);
    if (!source) {
        return false;
    }

    if (source->blocked) {
        return true;
    }

    const auto trigger = GetFingerSelectTrigger();
    if (trigger.requested) {
        ShowFingerSelector(*source, a_hand, trigger.inputDevice);
        return true;
    }

    if (ShouldOpenFingerSelectorForHand(*source, a_hand)) {
        ShowFingerSelector(*source, a_hand, GetPreferredInputDevice());
        return true;
    }

    if (a_hand == Core::Hand::kRight) {
        return false;
    }

    return ToggleMenuRingForTarget(*source, Core::kDefaultLeftTarget);
}
}
