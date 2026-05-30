#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace Inventory {
struct CustomEnchantmentKey {
    RE::FormID enchantmentFormID {0};
    std::uint16_t charge {0};
    bool removeOnUnequip {false};
    std::string playerDisplayName;
};

struct ExtraListIdentity {
    RE::FormID baseID {0};
    std::uint16_t uniqueID {0};

    [[nodiscard]] bool IsValid() const {
        return baseID != 0 && uniqueID != 0;
    }
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

enum class RightWornRingUnequipResult : std::uint8_t {
    kNone,
    kUnequipped,
    kProtected,
    kFailed,
};

struct EntryCustomSelection {
    RE::ExtraDataList* extraList {nullptr};
    std::optional<CustomEnchantmentKey> key;
    std::optional<ExtraListIdentity> identity;
    EntryCustomFailure failure {EntryCustomFailure::kNone};

    [[nodiscard]] bool HasCustomEnchantment() const;
};

struct CustomMatchState {
    RE::ExtraDataList* firstExtraList {nullptr};
    RE::ExtraDataList* rightWornExtraList {nullptr};
    std::int32_t count {0};
    bool rightWornProtected {false};

    [[nodiscard]] bool HasMatch() const;
    [[nodiscard]] bool CanWearSameKeyInBothHands() const;
};

struct FormOnlyMatchState {
    RE::ExtraDataList* rightWornExtraList {nullptr};
    std::int32_t count {0};
    bool rightWornProtected {false};
    bool rightWorn {false};

    [[nodiscard]] bool HasMatch() const;
    [[nodiscard]] bool CanWearSameFormInBothHands() const;
};

struct SourceRingState {
    RE::InventoryEntryData* entry {nullptr};
    RE::ExtraDataList* rightWornExtraList {nullptr};
    std::int32_t count {0};
    bool rightWornProtected {false};
    bool rightWorn {false};
    bool rightWornEnchanted {false};

    [[nodiscard]] bool HasRightWornEnchantment() const;
};

[[nodiscard]] bool operator==(const CustomEnchantmentKey& a_lhs, const CustomEnchantmentKey& a_rhs);
[[nodiscard]] bool operator!=(const CustomEnchantmentKey& a_lhs, const CustomEnchantmentKey& a_rhs);
[[nodiscard]] bool operator==(const ExtraListIdentity& a_lhs, const ExtraListIdentity& a_rhs);
[[nodiscard]] bool operator!=(const ExtraListIdentity& a_lhs, const ExtraListIdentity& a_rhs);
[[nodiscard]] bool HasCustomEnchantment(const RE::ExtraDataList* a_extraList);
[[nodiscard]] std::optional<CustomEnchantmentKey> ReadCustomEnchantmentKey(const RE::ExtraDataList* a_extraList);
[[nodiscard]] std::optional<ExtraListIdentity> ReadExtraListIdentity(const RE::ExtraDataList* a_extraList);
[[nodiscard]] std::optional<ExtraListIdentity> EnsureExtraListIdentity(
    RE::Actor& a_actor,
    const RE::TESBoundObject& a_object,
    RE::ExtraDataList& a_extraList
);
[[nodiscard]] bool MatchesCustomEnchantmentKey(const RE::ExtraDataList* a_extraList, const CustomEnchantmentKey& a_key);
[[nodiscard]] bool MatchesExtraListIdentity(const RE::ExtraDataList* a_extraList, const ExtraListIdentity& a_identity);
[[nodiscard]] bool MatchesCustomSelection(
    const RE::ExtraDataList* a_extraList,
    const CustomEnchantmentKey& a_key,
    const std::optional<ExtraListIdentity>& a_identity
);
[[nodiscard]] bool IsProtectedRingStack(RE::ExtraDataList* a_extraList);
[[nodiscard]] bool IsRightWorn(const RE::ExtraDataList* a_extraList);
[[nodiscard]] bool HasRightWornRing(RE::Actor& a_actor);
[[nodiscard]] bool HasProtectedRightWornRing(RE::Actor& a_actor);
[[nodiscard]] RightWornRingUnequipResult UnequipRightWornRing(RE::Actor& a_actor);
[[nodiscard]] std::optional<CustomEnchantmentData> ReadCustomEnchantment(const RE::ExtraDataList& a_extraList);
[[nodiscard]] bool MirrorCustomEnchantment(RE::ExtraDataList& a_target, const RE::ExtraDataList& a_source);
[[nodiscard]] RE::InventoryEntryData* FindEntry(RE::Actor& a_actor, const RE::TESBoundObject& a_object);
[[nodiscard]] std::int32_t GetCount(RE::Actor& a_actor, const RE::TESBoundObject& a_object);
[[nodiscard]] CustomMatchState FindCustomMatches(
    RE::InventoryEntryData* a_entry,
    const CustomEnchantmentKey& a_key,
    const std::optional<ExtraListIdentity>& a_identity = std::nullopt
);
[[nodiscard]] CustomMatchState FindSourceMatches(
    RE::Actor& a_actor,
    const RE::TESObjectARMO& a_ring,
    const CustomEnchantmentKey& a_key,
    const std::optional<ExtraListIdentity>& a_identity = std::nullopt
);
[[nodiscard]] FormOnlyMatchState FindFormOnlyMatches(RE::Actor& a_actor, const RE::TESObjectARMO& a_ring);
[[nodiscard]] EntryCustomSelection ResolveEntryCustomSelection(RE::InventoryEntryData& a_entry);
[[nodiscard]] RE::TESObjectARMO* AsRing(RE::TESBoundObject* a_object);
[[nodiscard]] RE::TESObjectARMO* AsRing(RE::TESForm* a_form);
[[nodiscard]] bool IsRing(const RE::TESObjectARMO* a_armor);
[[nodiscard]] SourceRingState GetSourceState(RE::Actor& a_actor, const RE::TESObjectARMO& a_ring);
[[nodiscard]] bool UnequipRightWornSource(RE::Actor& a_actor, RE::TESObjectARMO& a_ring);
}
