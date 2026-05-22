#include "Selection.h"

#include "BondOfMatrimony.h"
#include "ClonedEquipment.h"
#include "Inventory.h"
#include "Settings.h"
#include "UI.h"

#include <RE/S/SendUIMessage.h>

#include <utility>

namespace Selection {
namespace {
    std::mutex g_lock;
    std::array<State, kDisplaySlots.size()> g_selections;

    [[nodiscard]] RE::FormID ResolveFormID(
        SKSE::SerializationInterface& a_intfc,
        const RE::FormID a_formID,
        const std::string_view a_label
    ) {
        if (a_formID == 0) {
            return 0;
        }

        RE::FormID resolvedFormID = 0;
        if (!a_intfc.ResolveFormID(a_formID, resolvedFormID)) {
            logger::warn("Serialization: left-ring resolve failed | field={} | form={:08X}", a_label, a_formID);
            return 0;
        }
        return resolvedFormID;
    }

    void NotifyInventoryChanged(RE::Actor& a_actor, const RE::TESObjectARMO* a_ring) {
        RE::SendUIMessage::SendInventoryUpdateMessage(std::addressof(a_actor), a_ring);
        UI::RefreshRows();
    }

    [[nodiscard]] RE::PlayerCharacter* GetPlayer() {
        return RE::PlayerCharacter::GetSingleton();
    }

    [[nodiscard]] RE::TESObjectARMO* LookupSourceRing(const RE::FormID a_sourceFormID) {
        return RE::TESForm::LookupByID<RE::TESObjectARMO>(a_sourceFormID);
    }

    void ClearVirtualLeftSelection(RE::Actor& a_actor, const RE::TESObjectARMO& a_ring, const DisplaySlot a_channel) {
        Clear(a_channel);
        ClonedEquipment::Clear(a_channel);
        NotifyInventoryChanged(a_actor, std::addressof(a_ring));
    }

    void EnforceSameSourceInvariant(const DisplaySlot a_channel) {
        auto* player = GetPlayer();
        if (!player) {
            return;
        }

        const auto selection = Get(a_channel);
        auto* ring = GetSource(a_channel);
        if (!ring) {
            return;
        }

        const auto sourceState = Inventory::GetSourceState(*player, *ring);
        if (sourceState.count <= 0) {
            ClearVirtualLeftSelection(*player, *ring, a_channel);
            return;
        }

        if (selection.kind == Kind::kCustomEnchantment) {
            const auto customKey = selection.GetCustomKey();
            const auto sourceMatches = Inventory::FindSourceMatches(*player, *ring, customKey);
            if (!sourceMatches.HasMatch()) {
                ClearVirtualLeftSelection(*player, *ring, a_channel);
                return;
            }

            if (sourceMatches.rightWornExtraList && !sourceMatches.CanWearSameKeyInBothHands()) {
                ClearVirtualLeftSelection(*player, *ring, a_channel);
            }
            return;
        }

        if (sourceState.rightWorn && !sourceState.CanWearSameFormInBothHands()) {
            ClearVirtualLeftSelection(*player, *ring, a_channel);
        }
    }

    void MoveRightToLeft(const RE::FormID a_sourceFormID, const DisplaySlot a_channel) {
        auto* player = GetPlayer();
        if (!player) {
            return;
        }

        auto* ring = LookupSourceRing(a_sourceFormID);
        if (!ring) {
            return;
        }

        auto sourceState = Inventory::GetSourceState(*player, *ring);
        if (sourceState.count <= 0) {
            if (GetFormID(a_channel) == a_sourceFormID) {
                ClearVirtualLeftSelection(*player, *ring, a_channel);
            }
            return;
        }

        auto realInventoryChanged = false;
        if (sourceState.rightWorn && !sourceState.CanWearSameFormInBothHands()) {
            if (!Inventory::UnequipRightWornSource(*player, *ring)) {
                NotifyInventoryChanged(*player, ring);
                return;
            }

            realInventoryChanged = true;
            sourceState = Inventory::GetSourceState(*player, *ring);
            if (sourceState.rightWorn) {
                NotifyInventoryChanged(*player, ring);
                return;
            }
        }

        Set(ring, a_channel);
        ClonedEquipment::RequestRefresh();

        if (realInventoryChanged) {
            NotifyInventoryChanged(*player, ring);
        } else {
            UI::RefreshRows();
        }
    }

    void MoveRightToLeftCustom(
        const RE::FormID a_sourceFormID,
        const Inventory::CustomEnchantmentKey& a_customKey,
        const DisplaySlot a_channel
    ) {
        auto* player = GetPlayer();
        if (!player) {
            return;
        }

        auto* ring = LookupSourceRing(a_sourceFormID);
        if (!ring) {
            return;
        }

        auto sourceMatches = Inventory::FindSourceMatches(*player, *ring, a_customKey);
        if (!sourceMatches.HasMatch()) {
            NotifyInventoryChanged(*player, ring);
            return;
        }

        auto realInventoryChanged = false;
        if (sourceMatches.rightWornExtraList && !sourceMatches.CanWearSameKeyInBothHands()) {
            auto* equipManager = RE::ActorEquipManager::GetSingleton();
            if (!equipManager) {
                NotifyInventoryChanged(*player, ring);
                return;
            }

            equipManager->UnequipObject(
                player,
                ring,
                sourceMatches.rightWornExtraList,
                1,
                nullptr,
                true,
                false,
                false,
                true,
                nullptr
            );
            realInventoryChanged = true;

            sourceMatches = Inventory::FindSourceMatches(*player, *ring, a_customKey);
            if (!sourceMatches.HasMatch()
                || (sourceMatches.rightWornExtraList && !sourceMatches.CanWearSameKeyInBothHands())) {
                NotifyInventoryChanged(*player, ring);
                return;
            }
        }

        SetCustom(*ring, a_customKey, a_channel);
        ClonedEquipment::RequestRefresh();

        if (realInventoryChanged) {
            NotifyInventoryChanged(*player, ring);
        } else {
            UI::RefreshRows();
        }
    }

    void MoveLeftToRight(
        const RE::FormID a_sourceFormID,
        const std::optional<Inventory::CustomEnchantmentKey>& a_customKey,
        const RE::FormID a_equipSlotFormID,
        const bool a_forceEquip,
        const bool a_playSounds,
        const DisplaySlot a_channel
    ) {
        auto* player = GetPlayer();
        if (!player) {
            return;
        }

        auto* ring = LookupSourceRing(a_sourceFormID);
        if (!ring) {
            return;
        }

        const auto sourceState = Inventory::GetSourceState(*player, *ring);
        if (sourceState.count <= 0) {
            if (Get(a_channel).MatchesSource(a_sourceFormID)) {
                ClearVirtualLeftSelection(*player, *ring, a_channel);
            }
            return;
        }

        RE::ExtraDataList* equipExtraList = nullptr;
        if (a_customKey) {
            const auto sourceMatches = Inventory::FindSourceMatches(*player, *ring, *a_customKey);
            equipExtraList = sourceMatches.firstExtraList;
            if (!equipExtraList) {
                if (Get(a_channel).MatchesCustomEnchantment(a_sourceFormID, *a_customKey)) {
                    ClearVirtualLeftSelection(*player, *ring, a_channel);
                }
                NotifyInventoryChanged(*player, ring);
                return;
            }
        }

        const auto selection = Get(a_channel);
        if ((!a_customKey && selection.MatchesForm(a_sourceFormID))
            || (a_customKey && selection.MatchesCustomEnchantment(a_sourceFormID, *a_customKey))) {
            ClearVirtualLeftSelection(*player, *ring, a_channel);
        }

        auto* equipManager = RE::ActorEquipManager::GetSingleton();
        if (!equipManager) {
            NotifyInventoryChanged(*player, ring);
            return;
        }

        const auto* equipSlot = a_equipSlotFormID == 0 ? nullptr
                                                       : RE::TESForm::LookupByID<RE::BGSEquipSlot>(a_equipSlotFormID);
        if (a_equipSlotFormID != 0 && !equipSlot) {
            NotifyInventoryChanged(*player, ring);
            return;
        }

        equipManager->EquipObject(player, ring, equipExtraList, 1, equipSlot, true, a_forceEquip, a_playSounds, true);
        NotifyInventoryChanged(*player, ring);
    }

    struct SelectedDisplaySlot {
        DisplaySlot channel {DisplaySlot::kRegular};
        State selection;
    };

    [[nodiscard]] std::optional<SelectedDisplaySlot> FindSelectedChannelForSource(const RE::FormID a_sourceFormID) {
        auto selection = Get(DisplaySlot::kRegular);
        if (selection.MatchesSource(a_sourceFormID)) {
            return SelectedDisplaySlot {
                .channel = DisplaySlot::kRegular,
                .selection = selection,
            };
        }

        selection = Get(DisplaySlot::kBond);
        if (!selection.MatchesSource(a_sourceFormID)) {
            return std::nullopt;
        }

        return SelectedDisplaySlot {
            .channel = DisplaySlot::kBond,
            .selection = selection,
        };
    }

    [[nodiscard]] RE::FormID EquipSlotFormID(const RE::ObjectEquipParams& a_params) {
        return a_params.equipSlot ? a_params.equipSlot->GetFormID() : RE::FormID {0};
    }

    void QueueMoveLeftToRight(
        const RE::FormID a_sourceFormID,
        std::optional<Inventory::CustomEnchantmentKey> a_customKey,
        const RE::ObjectEquipParams& a_params,
        const DisplaySlot a_channel
    ) {
        stl::add_task([a_sourceFormID,
                          customKey = std::move(a_customKey),
                          equipSlotFormID = EquipSlotFormID(a_params),
                          forceEquip = a_params.forceEquip,
                          playSounds = a_params.playEquipSounds,
                          a_channel] {
            MoveLeftToRight(a_sourceFormID, customKey, equipSlotFormID, forceEquip, playSounds, a_channel);
        });
    }

    [[nodiscard]] bool InterceptCustomRightEquip(
        RE::Actor& a_actor,
        const RE::TESObjectARMO& a_ring,
        const State& a_selection,
        const RE::ObjectEquipParams& a_params,
        const DisplaySlot a_channel
    ) {
        const auto sourceFormID = a_ring.GetFormID();
        const auto selectedCustomKey = a_selection.GetCustomKey();
        if (a_params.extraDataList) {
            const auto paramsMatch = Inventory::MatchesCustomEnchantmentKey(a_params.extraDataList, selectedCustomKey);
            if (!paramsMatch) {
                return false;
            }

            const auto sourceMatches = Inventory::FindSourceMatches(a_actor, a_ring, selectedCustomKey);
            if (sourceMatches.CanWearSameKeyInBothHands()) {
                return false;
            }

            QueueMoveLeftToRight(sourceFormID, selectedCustomKey, a_params, a_channel);
            return true;
        }

        const auto sourceState = Inventory::GetSourceState(a_actor, a_ring);
        if (sourceState.CanWearSameFormInBothHands()) {
            return false;
        }

        QueueMoveLeftToRight(sourceFormID, selectedCustomKey, a_params, a_channel);
        return true;
    }
    void ClearRestoreForChannel(const DisplaySlot a_channel) {
        ArmorClones::ClearRestore(a_channel);
    }

    void SetSelection(RE::TESObjectARMO* a_ring, const DisplaySlot a_channel) {
        std::scoped_lock lock(g_lock);
        if (a_ring) {
            g_selections[ToIndex(a_channel)] = State {
                .kind = Kind::kFormOnly,
                .sourceFormID = a_ring->GetFormID(),
            };
        } else {
            g_selections[ToIndex(a_channel)] = {};
        }
        ClearRestoreForChannel(a_channel);
    }

    void SetCustomSelection(
        RE::TESObjectARMO& a_ring,
        Inventory::CustomEnchantmentKey a_key,
        const DisplaySlot a_channel
    ) {
        std::scoped_lock lock(g_lock);
        g_selections[ToIndex(a_channel)] = State {
            .kind = Kind::kCustomEnchantment,
            .sourceFormID = a_ring.GetFormID(),
            .customKey = std::move(a_key),
        };
        ClearRestoreForChannel(a_channel);
    }
}

void Set(RE::TESObjectARMO* a_ring, const DisplaySlot a_channel) {
    SetSelection(a_ring, a_channel);
}

void SetCustom(RE::TESObjectARMO& a_ring, Inventory::CustomEnchantmentKey a_key, const DisplaySlot a_channel) {
    SetCustomSelection(a_ring, std::move(a_key), a_channel);
}

void Clear(const DisplaySlot a_channel) {
    Set(nullptr, a_channel);
}

RE::TESObjectARMO* GetSource(const DisplaySlot a_channel) {
    const auto formID = GetFormID(a_channel);
    return Inventory::AsRing(RE::TESForm::LookupByID(formID));
}

RE::FormID GetFormID(const DisplaySlot a_channel) {
    std::scoped_lock lock(g_lock);
    return g_selections[ToIndex(a_channel)].sourceFormID;
}

State Get(const DisplaySlot a_channel) {
    std::scoped_lock lock(g_lock);
    return g_selections[ToIndex(a_channel)];
}

void Load(const Snapshot& a_state, SKSE::SerializationInterface& a_intfc) {
    std::scoped_lock lock(g_lock);
    const auto loadSelection = [&](const DisplaySlot a_channel, const State& a_storedState) {
        g_selections[ToIndex(a_channel)] = {};
        ArmorClones::ClearRestore(a_channel);

        if (a_storedState.kind == Kind::kNone || a_storedState.sourceFormID == 0) {
            return;
        }

        const auto sourceFormID = ResolveFormID(a_intfc, a_storedState.sourceFormID, "source form"sv);
        if (sourceFormID == 0) {
            return;
        }

        if (a_channel
            == DisplaySlot::kBond
            && (!Settings::GetSingleton()->IsBondOfMatrimonyEnabled() || !BondOfMatrimony::IsBond(sourceFormID))) {
            logger::warn(
                "Serialization: Bond selection cleared | source={:08X} | reason=featureDisabledOrSourceMismatch",
                sourceFormID
            );
            return;
        }

        if (a_storedState.kind == Kind::kFormOnly) {
            g_selections[ToIndex(a_channel)] = State {
                .kind = Kind::kFormOnly,
                .sourceFormID = sourceFormID,
            };
        } else if (a_storedState.kind == Kind::kCustomEnchantment) {
            auto customKey = a_storedState.customKey;
            customKey.enchantmentFormID = ResolveFormID(a_intfc, customKey.enchantmentFormID, "custom enchantment"sv);
            if (customKey.enchantmentFormID == 0) {
                logger::warn(
                    "Serialization: custom selection cleared | channel={} | source={:08X} | enchantment={:08X} | charge={} | reason=enchantmentResolveFailed",
                    DisplaySlotLabel(a_channel),
                    sourceFormID,
                    a_storedState.customKey.enchantmentFormID,
                    a_storedState.customKey.charge
                );
                return;
            }

            if (!RE::TESForm::LookupByID<RE::EnchantmentItem>(customKey.enchantmentFormID)) {
                logger::warn(
                    "Serialization: custom selection cleared | channel={} | source={:08X} | enchantment={:08X} | charge={} | reason=enchantmentMissing",
                    DisplaySlotLabel(a_channel),
                    sourceFormID,
                    customKey.enchantmentFormID,
                    customKey.charge
                );
                return;
            }

            g_selections[ToIndex(a_channel)] = State {
                .kind = Kind::kCustomEnchantment,
                .sourceFormID = sourceFormID,
                .customKey = std::move(customKey),
            };
        } else {
            logger::warn(
                "Serialization: selection cleared | channel={} | source={:08X} | kind={} | reason=unsupportedKind",
                DisplaySlotLabel(a_channel),
                sourceFormID,
                std::to_underlying(a_storedState.kind)
            );
            return;
        }

        ArmorClones::RequireRestore(a_channel, g_selections[ToIndex(a_channel)].sourceFormID);
    };

    loadSelection(DisplaySlot::kRegular, a_state.regular);
    loadSelection(DisplaySlot::kBond, a_state.bond);
    if (Settings::GetSingleton()->IsBondOfMatrimonyEnabled()
        && BondOfMatrimony::IsBond(g_selections[ToIndex(DisplaySlot::kRegular)].sourceFormID)) {
        g_selections[ToIndex(DisplaySlot::kRegular)] = {};
        ArmorClones::ClearRestore(DisplaySlot::kRegular);
    }
}

Snapshot GetSnapshot() {
    std::scoped_lock lock(g_lock);
    return Snapshot {
        .regular = g_selections[ToIndex(DisplaySlot::kRegular)],
        .bond = g_selections[ToIndex(DisplaySlot::kBond)],
    };
}

std::vector<ArmorClones::CloneKey> GetCloneKeys() {
    std::scoped_lock lock(g_lock);
    std::vector<ArmorClones::CloneKey> keys;
    keys.reserve(kDisplaySlots.size());
    for (const auto channel : kDisplaySlots) {
        const auto& selection = g_selections[ToIndex(channel)];
        if (selection.sourceFormID != 0) {
            keys.push_back(
                ArmorClones::CloneKey {
                    .channel = channel,
                    .sourceArmorFormID = selection.sourceFormID,
                }
            );
        }
    }
    return keys;
}

void Revert() {
    std::scoped_lock lock(g_lock);
    g_selections = {};
    for (const auto channel : kDisplaySlots) {
        ArmorClones::ClearRestore(channel);
    }
}

void NormalizeAfterSettingsReload() {
    std::scoped_lock lock(g_lock);
    if (!Settings::GetSingleton()->IsBondOfMatrimonyEnabled()) {
        g_selections[ToIndex(DisplaySlot::kBond)] = {};
        ArmorClones::ClearRestore(DisplaySlot::kBond);
        return;
    }

    const auto normalSource = g_selections[ToIndex(DisplaySlot::kRegular)].sourceFormID;
    if (BondOfMatrimony::IsBond(normalSource)) {
        g_selections[ToIndex(DisplaySlot::kRegular)] = {};
        ArmorClones::ClearRestore(DisplaySlot::kRegular);
    }
}

void RequestMove(const RE::FormID a_sourceFormID, const DisplaySlot a_channel) {
    stl::add_task([a_sourceFormID, a_channel] {
        MoveRightToLeft(a_sourceFormID, a_channel);
    });
}

void RequestCustomMove(
    const RE::FormID a_sourceFormID,
    Inventory::CustomEnchantmentKey a_key,
    const DisplaySlot a_channel
) {
    stl::add_task([a_sourceFormID, key = std::move(a_key), a_channel] {
        MoveRightToLeftCustom(a_sourceFormID, key, a_channel);
    });
}

bool InterceptRightEquip(RE::Actor& a_actor, const RE::TESObjectARMO& a_ring, const RE::ObjectEquipParams& a_params) {
    const auto sourceFormID = a_ring.GetFormID();
    const auto selectedChannel = FindSelectedChannelForSource(sourceFormID);
    if (!selectedChannel) {
        return false;
    }

    const auto channel = selectedChannel->channel;
    if (channel == DisplaySlot::kBond && !Settings::GetSingleton()->IsBondOfMatrimonyEnabled()) {
        return false;
    }

    const auto& selection = selectedChannel->selection;
    if (selection.kind == Kind::kCustomEnchantment) {
        return InterceptCustomRightEquip(a_actor, a_ring, selection, a_params, channel);
    }

    const auto sourceState = Inventory::GetSourceState(a_actor, a_ring);
    if (sourceState.CanWearSameFormInBothHands()) {
        return false;
    }

    QueueMoveLeftToRight(sourceFormID, std::nullopt, a_params, channel);
    return true;
}

void QueueCheck() {
    stl::add_task([] {
        for (const auto channel : kDisplaySlots) {
            EnforceSameSourceInvariant(channel);
        }
    });
}

void OnContainerChanged(const RE::TESContainerChangedEvent& a_event) {
    const auto snapshot = GetSnapshot();
    if ((snapshot.regular.sourceFormID == 0 || a_event.baseObj != snapshot.regular.sourceFormID)
        && (snapshot.bond.sourceFormID == 0 || a_event.baseObj != snapshot.bond.sourceFormID)) {
        return;
    }

    auto* player = GetPlayer();
    if (!player) {
        return;
    }

    const auto playerFormID = player->GetFormID();
    if (a_event.oldContainer != playerFormID && a_event.newContainer != playerFormID) {
        return;
    }

    QueueCheck();
}
}
