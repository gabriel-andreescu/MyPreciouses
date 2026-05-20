#pragma once

#include "Inventory.h"
#include "RuntimeClones.h"
#include "Slots.h"

#include <array>
#include <vector>

namespace Selection {
enum class Kind : std::uint32_t {
    kNone = 0,
    kFormOnly = 1,
    kCustomEnchantment = 2,
};

struct State {
    Kind kind {Kind::kNone};
    RE::FormID sourceFormID {0};
    Inventory::CustomEnchantmentKey customKey;

    [[nodiscard]] Inventory::CustomEnchantmentKey GetCustomKey() const {
        return customKey;
    }

    [[nodiscard]] bool MatchesForm(RE::FormID a_sourceFormID) const {
        return kind == Kind::kFormOnly && sourceFormID == a_sourceFormID;
    }

    [[nodiscard]] bool MatchesCustomEnchantment(
        RE::FormID a_sourceFormID,
        const Inventory::CustomEnchantmentKey& a_key
    ) const {
        return kind == Kind::kCustomEnchantment && sourceFormID == a_sourceFormID && customKey == a_key;
    }

    [[nodiscard]] bool MatchesSource(RE::FormID a_sourceFormID) const {
        return kind != Kind::kNone && sourceFormID == a_sourceFormID;
    }

    [[nodiscard]] bool operator==(const State&) const = default;
};

struct Snapshot {
    State regular;
    State bond;
};

void Set(RE::TESObjectARMO* a_ring, DisplaySlot a_channel = DisplaySlot::kRegular);
void SetCustom(
    RE::TESObjectARMO& a_ring,
    Inventory::CustomEnchantmentKey a_key,
    DisplaySlot a_channel = DisplaySlot::kRegular
);
void Clear(DisplaySlot a_channel = DisplaySlot::kRegular);

[[nodiscard]] RE::TESObjectARMO* GetSource(DisplaySlot a_channel = DisplaySlot::kRegular);
[[nodiscard]] RE::FormID GetFormID(DisplaySlot a_channel = DisplaySlot::kRegular);
[[nodiscard]] State Get(DisplaySlot a_channel = DisplaySlot::kRegular);

void Load(const Snapshot& a_state, SKSE::SerializationInterface& a_intfc);
[[nodiscard]] Snapshot GetSnapshot();
[[nodiscard]] std::vector<RuntimeClones::CloneKey> GetCloneKeys();
void Revert();
void NormalizeAfterSettingsReload();

void RequestMove(RE::FormID a_sourceFormID, DisplaySlot a_channel = DisplaySlot::kRegular);
void RequestCustomMove(
    RE::FormID a_sourceFormID,
    Inventory::CustomEnchantmentKey a_key,
    DisplaySlot a_channel = DisplaySlot::kRegular
);
[[nodiscard]] bool InterceptRightEquip(
    RE::Actor& a_actor,
    const RE::TESObjectARMO& a_ring,
    const RE::ObjectEquipParams& a_params
);
void QueueCheck();
void OnContainerChanged(const RE::TESContainerChangedEvent& a_event);
}
