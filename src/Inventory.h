#pragma once

#include "Core/ItemSource.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Inventory {
struct CustomEnchantmentData {
    RE::EnchantmentItem* enchantment {nullptr};
    std::uint16_t charge {0};
    bool removeOnUnequip {false};
    std::optional<std::string> playerDisplayName;
};

enum class EntryCustomFailure : std::uint32_t {
    kNone = 0,
    kMultipleCustomEnchantments = 1,
};

struct EntryRingSource {
    RE::TESObjectARMO* ring {nullptr};
    Core::ItemSource source;
    RE::ExtraDataList* sourceExtraList {nullptr};
    bool vanillaRingSlotEquipped {false};
    EntryCustomFailure customFailure {EntryCustomFailure::kNone};
};

struct CustomSourceMatch {
    RE::ExtraDataList* firstExtraList {nullptr};
    RE::ExtraDataList* rightWornExtraList {nullptr};
    std::int32_t count {0};
    bool rightWornProtected {false};

    [[nodiscard]] bool HasMatch() const;
};

struct FormOnlySourceMatch {
    RE::ExtraDataList* rightWornExtraList {nullptr};
    std::int32_t count {0};
    bool rightWornProtected {false};
    bool rightWorn {false};

    [[nodiscard]] bool HasMatch() const;
};

struct RightWornRing {
    RE::TESObjectARMO* ring {nullptr};
    RE::ExtraDataList* extraList {nullptr};
    bool protectedStack {false};
};

struct RingInventoryState {
    RE::ExtraDataList* rightWornExtraList {nullptr};
    bool rightWorn {false};
};

[[nodiscard]] bool HasCustomEnchantment(const RE::ExtraDataList* a_extraList);
[[nodiscard]] bool MatchesCustomSelection(
    const RE::ExtraDataList* a_extraList,
    const Core::CustomEnchantmentSignature& a_signature,
    const std::optional<Core::ExtraUniqueIDKey>& a_uniqueID
);
[[nodiscard]] bool IsProtectedRingStack(RE::ExtraDataList* a_extraList);
[[nodiscard]] bool IsRightWorn(const RE::ExtraDataList* a_extraList);
[[nodiscard]] std::optional<RightWornRing> FindRightWornRing(RE::Actor& a_actor);
[[nodiscard]] bool HasRightWornRing(RE::Actor& a_actor);
[[nodiscard]] bool HasProtectedRightWornRing(RE::Actor& a_actor);
[[nodiscard]] std::optional<CustomEnchantmentData> ReadCustomEnchantment(const RE::ExtraDataList& a_extraList);
[[nodiscard]] bool MirrorCustomEnchantment(RE::ExtraDataList& a_target, const RE::ExtraDataList& a_source);
[[nodiscard]] RE::InventoryEntryData* FindEntry(RE::Actor& a_actor, const RE::TESBoundObject& a_object);
[[nodiscard]] std::int32_t GetCount(RE::Actor& a_actor, const RE::TESBoundObject& a_object);
[[nodiscard]] CustomSourceMatch FindCustomSourceMatches(
    RE::Actor& a_actor,
    const RE::TESObjectARMO& a_ring,
    const Core::CustomEnchantmentSignature& a_signature,
    const std::optional<Core::ExtraUniqueIDKey>& a_uniqueID = std::nullopt
);
[[nodiscard]] FormOnlySourceMatch FindFormOnlySourceMatches(RE::Actor& a_actor, const RE::TESObjectARMO& a_ring);
[[nodiscard]] std::optional<EntryRingSource> ResolveEntryRingSource(
    RE::Actor& a_actor,
    RE::InventoryEntryData& a_entry
);
[[nodiscard]] RE::TESObjectARMO* AsRing(RE::TESBoundObject* a_object);
[[nodiscard]] RE::TESObjectARMO* AsRing(RE::TESForm* a_form);
[[nodiscard]] bool IsRing(const RE::TESObjectARMO* a_armor);
[[nodiscard]] RingInventoryState GetRingInventoryState(RE::Actor& a_actor, const RE::TESObjectARMO& a_ring);
}
