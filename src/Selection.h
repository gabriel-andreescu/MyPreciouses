#pragma once

#include "Inventory.h"
#include "RingTargets.h"

#include <array>
#include <optional>

namespace Selection {
enum class Kind : std::uint32_t {
    kNone = 0,
    kFormOnly = 1,
    kCustomEnchantment = 2,
};

struct State {
    Kind kind {Kind::kNone};
    RE::FormID sourceFormID {0};
    RE::FormID restoredEffectSourceFormID {0};
    Inventory::CustomEnchantmentKey customKey;
    std::optional<Inventory::ExtraListIdentity> customIdentity;

    [[nodiscard]] Inventory::CustomEnchantmentKey GetCustomKey() const {
        return customKey;
    }

    [[nodiscard]] std::optional<Inventory::ExtraListIdentity> GetCustomIdentity() const {
        return customIdentity;
    }

    [[nodiscard]] bool MatchesForm(RE::FormID a_sourceFormID) const {
        return kind == Kind::kFormOnly && sourceFormID == a_sourceFormID;
    }

    [[nodiscard]] bool MatchesCustomEnchantment(
        RE::FormID a_sourceFormID,
        const Inventory::CustomEnchantmentKey& a_key,
        const std::optional<Inventory::ExtraListIdentity>& a_identity = std::nullopt
    ) const {
        if (kind != Kind::kCustomEnchantment || sourceFormID != a_sourceFormID || customKey != a_key) {
            return false;
        }

        return !customIdentity || (a_identity && *customIdentity == *a_identity);
    }

    [[nodiscard]] bool MatchesSource(RE::FormID a_sourceFormID) const {
        return kind != Kind::kNone && sourceFormID == a_sourceFormID;
    }

    [[nodiscard]] bool operator==(const State&) const = default;
};

struct Snapshot {
    std::array<State, kRingTargets.size()> targets;
};

struct RingActionResult {
    bool selectionChanged {false};
    bool inventoryChanged {false};

    [[nodiscard]] bool ChangedState() const {
        return selectionChanged || inventoryChanged;
    }
};

[[nodiscard]] bool Set(RE::TESObjectARMO* a_ring, RingTarget a_target);
[[nodiscard]] bool SetCustom(
    RE::TESObjectARMO& a_ring,
    Inventory::CustomEnchantmentKey a_key,
    std::optional<Inventory::ExtraListIdentity> a_identity,
    RingTarget a_target
);
void Clear(RingTarget a_target);
void SetRestoredEffectSourceFormID(RingTarget a_target, RE::FormID a_effectSourceFormID);

[[nodiscard]] RE::TESObjectARMO* GetSource(RingTarget a_target);
[[nodiscard]] RE::FormID GetFormID(RingTarget a_target);
[[nodiscard]] State Get(RingTarget a_target);
[[nodiscard]] bool IsSelected(
    RingTarget a_target,
    RE::FormID a_sourceFormID,
    const std::optional<Inventory::CustomEnchantmentKey>& a_customKey = std::nullopt,
    const std::optional<Inventory::ExtraListIdentity>& a_identity = std::nullopt
);

void Load(const Snapshot& a_state, SKSE::SerializationInterface& a_intfc);
[[nodiscard]] Snapshot GetSnapshot();
void Revert();

[[nodiscard]] RingActionResult MoveVanillaRingSlotFormToVirtual(RE::FormID a_sourceFormID, RingTarget a_target);
[[nodiscard]] RingActionResult MoveVanillaRingSlotCustomToVirtual(
    RE::FormID a_sourceFormID,
    const Inventory::CustomEnchantmentKey& a_customKey,
    std::optional<Inventory::ExtraListIdentity> a_customIdentity,
    RingTarget a_target
);
void QueueVanillaRingSlotFormToVirtual(RE::FormID a_sourceFormID, RingTarget a_target);
void QueueVanillaRingSlotCustomToVirtual(
    RE::FormID a_sourceFormID,
    Inventory::CustomEnchantmentKey a_key,
    std::optional<Inventory::ExtraListIdentity> a_identity,
    RingTarget a_target
);
[[nodiscard]] RingActionResult ToggleVanillaRingSlotForm(RE::FormID a_sourceFormID);
[[nodiscard]] RingActionResult ToggleVanillaRingSlotCustom(
    RE::FormID a_sourceFormID,
    const Inventory::CustomEnchantmentKey& a_customKey,
    std::optional<Inventory::ExtraListIdentity> a_customIdentity
);
[[nodiscard]] bool InterceptRightEquip(
    RE::Actor& a_actor,
    const RE::TESObjectARMO& a_ring,
    const RE::ObjectEquipParams& a_params
);
void QueueCheck();
void OnContainerChanged(const RE::TESContainerChangedEvent& a_event);
}
