#pragma once

#include "Core/ActorKey.h"

#include <cstdint>
#include <string>

namespace UI::RingItemRows {
inline constexpr auto kEmptyRingDisplayName = "-";

enum class RowStampResult : std::uint8_t {
    kIgnored,
    kUnchanged,
    kChanged,
};

struct RingHintState {
    bool canUseEquip {false};
    bool canShowFingerSelect {false};
};

[[nodiscard]] std::string GetRingDisplayName(const RE::TESObjectARMO& a_ring);
[[nodiscard]] RowStampResult StampRingEntry(
    RE::GFxValue& a_object,
    RE::InventoryEntryData& a_entry,
    Core::ActorKey a_actor,
    bool a_updateRowLabel
);
[[nodiscard]] RowStampResult ClearRingEntry(RE::GFxValue& a_object);
[[nodiscard]] RowStampResult RefreshStampedRingEntry(RE::GFxValue& a_entryObject, Core::ActorKey a_actor);
[[nodiscard]] RingHintState GetRingEntryHintState(RE::InventoryEntryData& a_entry, Core::ActorKey a_actor);
[[nodiscard]] bool CanUseRingEquipHint(const RE::GFxValue& a_entryObject);
[[nodiscard]] bool CanShowFingerSelectHint(const RE::GFxValue& a_entryObject);
}
