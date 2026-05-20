#pragma once

#include <optional>
#include <string>

namespace Inventory {
struct CustomEnchantmentKey {
    RE::FormID enchantmentFormID {0};
    std::uint16_t charge {0};
    bool removeOnUnequip {false};
    std::string playerDisplayName;
};

struct CustomEnchantmentData {
    RE::EnchantmentItem* enchantment {nullptr};
    std::uint16_t charge {0};
    bool removeOnUnequip {false};
    std::optional<std::string> playerDisplayName;
};

enum class EntryCustomFailure : std::uint32_t {
    kNone = 0,
    kMultipleCustomKeys = 1,
};

struct EntryCustomSelection {
    RE::ExtraDataList* extraList {nullptr};
    std::optional<CustomEnchantmentKey> key;
    EntryCustomFailure failure {EntryCustomFailure::kNone};

    [[nodiscard]] bool HasCustomEnchantment() const;
};

struct CustomMatchState {
    RE::ExtraDataList* firstExtraList {nullptr};
    RE::ExtraDataList* rightWornExtraList {nullptr};
    std::int32_t count {0};

    [[nodiscard]] bool HasMatch() const;
    [[nodiscard]] bool CanWearSameKeyInBothHands() const;
};

struct SourceRingState {
    RE::InventoryEntryData* entry {nullptr};
    RE::ExtraDataList* rightWornExtraList {nullptr};
    std::int32_t count {0};
    bool rightWorn {false};
    bool rightWornEnchanted {false};

    [[nodiscard]] bool CanWearSameFormInBothHands() const;
    [[nodiscard]] bool HasRightWornEnchantment() const;
};

[[nodiscard]] bool operator==(const CustomEnchantmentKey& a_lhs, const CustomEnchantmentKey& a_rhs);
[[nodiscard]] bool operator!=(const CustomEnchantmentKey& a_lhs, const CustomEnchantmentKey& a_rhs);
[[nodiscard]] bool HasCustomEnchantment(const RE::ExtraDataList* a_extraList);
[[nodiscard]] std::optional<CustomEnchantmentKey> ReadCustomEnchantmentKey(const RE::ExtraDataList* a_extraList);
[[nodiscard]] bool MatchesCustomEnchantmentKey(const RE::ExtraDataList* a_extraList, const CustomEnchantmentKey& a_key);
[[nodiscard]] bool IsRightWorn(const RE::ExtraDataList* a_extraList);
[[nodiscard]] std::optional<CustomEnchantmentData> ReadCustomEnchantment(const RE::ExtraDataList& a_extraList);
[[nodiscard]] bool MirrorCustomEnchantment(RE::ExtraDataList& a_target, const RE::ExtraDataList& a_source);
[[nodiscard]] RE::InventoryEntryData* FindEntry(RE::Actor& a_actor, const RE::TESBoundObject& a_object);
[[nodiscard]] std::int32_t GetCount(RE::Actor& a_actor, const RE::TESBoundObject& a_object);
[[nodiscard]] CustomMatchState FindCustomMatches(RE::InventoryEntryData* a_entry, const CustomEnchantmentKey& a_key);
[[nodiscard]] CustomMatchState FindSourceMatches(
    RE::Actor& a_actor,
    const RE::TESObjectARMO& a_ring,
    const CustomEnchantmentKey& a_key
);
[[nodiscard]] EntryCustomSelection ResolveCustomSelection(RE::InventoryEntryData& a_entry);
[[nodiscard]] RE::TESObjectARMO* AsRing(RE::TESBoundObject* a_object);
[[nodiscard]] RE::TESObjectARMO* AsRing(RE::TESForm* a_form);
[[nodiscard]] bool IsRing(const RE::TESObjectARMO* a_armor);
[[nodiscard]] SourceRingState GetSourceState(RE::Actor& a_actor, const RE::TESObjectARMO& a_ring);
[[nodiscard]] bool UnequipRightWornSource(RE::Actor& a_actor, RE::TESObjectARMO& a_ring);
}
