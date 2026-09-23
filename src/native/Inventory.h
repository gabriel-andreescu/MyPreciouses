#pragma once

#include <RE/Skyrim.h> // IWYU pragma: keep

#include "Core/ItemSource.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Inventory {
[[nodiscard]] std::uint64_t SelectionRevision(RE::FormID a_actor);
void InvalidateSelections(RE::FormID a_actor);
void RevertSelections();
enum class EntryCustomFailure : std::uint32_t {
    kNone = 0,
    kMultipleCustomEnchantments = 1,
};

struct EntryRingSource {
    RE::TESObjectARMO* ring {nullptr};
    Core::ItemSource source;
    bool vanillaRingSlotEquipped {false};
    EntryCustomFailure customFailure {EntryCustomFailure::kNone};
    std::vector<Core::ItemSource> rowSources;
};

enum class EntryResolveScope : std::uint8_t {
    kActorInventory,
    kMenuRow,
};

struct CustomSourceMatch {
    RE::ExtraDataList* firstExtraList {nullptr};
    RE::ExtraDataList* rightWornExtraList {nullptr};
    std::int32_t count {0};
    bool rightWornProtected {false};

    [[nodiscard]] bool HasMatch() const;
};

struct SourceMatch {
    RE::ExtraDataList* firstExtraList {nullptr};
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

enum class RightWornRingUnequipResult : std::uint8_t {
    kNone,
    kUnequipped,
    kProtected,
    kFailed,
};

struct RingInventoryState {
    RE::ExtraDataList* rightWornExtraList {nullptr};
    bool rightWorn {false};
};

[[nodiscard]] bool HasCustomEnchantment(const RE::ExtraDataList* a_extraList);
[[nodiscard]] std::optional<Core::ExtraUniqueIDKey> EnsureExtraUniqueIDKey(
    RE::Actor& a_actor,
    const RE::TESBoundObject& a_object,
    RE::ExtraDataList& a_extraList
);
[[nodiscard]] bool MatchesCustomSelection(
    const RE::ExtraDataList* a_extraList,
    const Core::CustomEnchantmentSignature& a_signature,
    const std::optional<Core::ExtraUniqueIDKey>& a_uniqueID
);
[[nodiscard]] bool IsUnequipProtectedRingStack(RE::ExtraDataList* a_extraList);
[[nodiscard]] bool HasRightWornFlag(const RE::ExtraDataList* a_extraList);
[[nodiscard]] bool IsRingSourceRightWorn(
    RE::Actor& a_actor,
    const RE::TESObjectARMO& a_ring,
    const RE::ExtraDataList* a_extraList
);
[[nodiscard]] std::optional<RightWornRing> FindRightWornRing(RE::Actor& a_actor);
[[nodiscard]] bool RightWornRingMatchesSource(
    const RightWornRing& a_rightWorn,
    const RE::TESObjectARMO& a_ring,
    const Core::ItemSource& a_source
);
[[nodiscard]] bool HasProtectedRightWornRing(RE::Actor& a_actor);
[[nodiscard]] RightWornRingUnequipResult UnequipRightWornRing(RE::Actor& a_actor);
[[nodiscard]] std::optional<std::string_view> ReadPlayerDisplayName(const RE::ExtraDataList& a_extraList);
[[nodiscard]] RE::InventoryEntryData* FindEntry(RE::Actor& a_actor, const RE::TESBoundObject& a_object);
[[nodiscard]] std::int32_t GetCount(RE::Actor& a_actor, const RE::TESBoundObject& a_object);
[[nodiscard]] CustomSourceMatch FindCustomSourceMatches(
    RE::Actor& a_actor,
    const RE::TESObjectARMO& a_ring,
    const Core::CustomEnchantmentSignature& a_signature,
    const std::optional<Core::ExtraUniqueIDKey>& a_uniqueID = std::nullopt
);
[[nodiscard]] SourceMatch FindFormOnlySourceMatches(RE::Actor& a_actor, const RE::TESObjectARMO& a_ring);
[[nodiscard]] SourceMatch FindSourceMatches(RE::Actor& a_actor, const Core::ItemSource& a_source);
[[nodiscard]] bool MatchesSource(const RE::ExtraDataList* a_extraList, const Core::ItemSource& a_source);
[[nodiscard]] std::optional<Core::ItemSource> AcquireCopy(
    RE::Actor& a_actor,
    const Core::ItemSource& a_source,
    std::span<const Core::ItemSource> a_claimed,
    bool a_untrackedOnly = false
);
[[nodiscard]] std::optional<EntryRingSource> ResolveEntryRingSource(
    RE::Actor& a_actor,
    RE::InventoryEntryData& a_entry,
    EntryResolveScope a_scope = EntryResolveScope::kActorInventory
);
[[nodiscard]] std::optional<EntryRingSource> PrepareMenuRingSelection(
    RE::Actor& a_actor,
    RE::InventoryEntryData& a_entry,
    std::span<const Core::ItemSource> a_claimed
);
[[nodiscard]] RE::TESObjectARMO* AsRing(RE::TESBoundObject* a_object);
[[nodiscard]] RE::TESObjectARMO* AsRing(RE::TESForm* a_form);
[[nodiscard]] bool IsRing(const RE::TESObjectARMO* a_armor);
[[nodiscard]] bool HasClothingRingKeyword(const RE::TESObjectARMO* a_armor);
[[nodiscard]] RingInventoryState GetRingInventoryState(RE::Actor& a_actor, const RE::TESObjectARMO& a_ring);
}
