#include "Inventory.h"

#include <RE/Skyrim.h> // IWYU pragma: keep
#include <REL/Relocation.h>
#include <SKSE/SKSE.h> // IWYU pragma: keep

#include "Compatibility/Vanilla.h"
#include "Core/ItemSource.h"
#include "SourceModelFootprints.h"

#include <RE/E/ExtraCannotWear.h>
#include <RE/E/ExtraOutfitItem.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Inventory {
namespace {
    bool RetainsInventoryIdentity(const RE::TESForm& a_form) {
        return (a_form.formFlags & RE::TESForm::RecordFlags::kFormRetainsID) != 0;
    }

    std::mutex selectionLock;
    std::uint64_t nextSelectionRevision {0};
    auto& SelectionRevisions() {
        static std::unordered_map<RE::FormID, std::uint64_t> revisions;
        return revisions;
    }
    constexpr auto kClothingRingKeyword = std::string_view {"ClothingRing"};
    constexpr auto kMaxRightWornRingUnequipAttempts = std::uint8_t {10};

    enum class RightWornExtraListFilter {
        kAny,
        kFormOnly,
    };

    struct EntryCustomSelection {
        RE::ExtraDataList* extraList {nullptr};
        std::optional<Core::CustomEnchantmentSignature> signature;
        std::optional<Core::ExtraUniqueIDKey> uniqueID;
        EntryCustomFailure failure {EntryCustomFailure::kNone};
    };

    struct MenuEntryExtraListResolution {
        EntryCustomSelection customSelection;
        bool hasFormOnlySource {false};
        bool formOnlyRightWorn {false};
    };

    [[nodiscard]] RE::ExtraDataList* FindRightWornExtraList(
        RE::InventoryEntryData const* a_entry,
        const RightWornExtraListFilter a_filter
    ) {
        if (!a_entry || !a_entry->extraLists) {
            return nullptr;
        }

        for (auto* extraList : *a_entry->extraLists) {
            if (Inventory::HasRightWornFlag(extraList)
                && (a_filter == RightWornExtraListFilter::kAny || !Inventory::HasCustomEnchantment(extraList))) {
                return extraList;
            }
        }

        return nullptr;
    }

    [[nodiscard]] RE::ExtraDataList* FindRightWornExtraList(RE::InventoryEntryData const* a_entry) {
        return FindRightWornExtraList(a_entry, RightWornExtraListFilter::kAny);
    }

    [[nodiscard]] RE::ExtraDataList* FindRightWornFormOnlyExtraList(RE::InventoryEntryData const* a_entry) {
        return FindRightWornExtraList(a_entry, RightWornExtraListFilter::kFormOnly);
    }

    [[nodiscard]] bool IsFormOnlyRightWorn(RE::InventoryEntryData const& a_entry) {
        if (!a_entry.extraLists) {
            return a_entry.IsWorn(false);
        }

        return FindRightWornFormOnlyExtraList(std::addressof(a_entry)) != nullptr;
    }

    [[nodiscard]] bool EntryContainsExtraList(
        const RE::InventoryEntryData* a_entry,
        const RE::ExtraDataList* a_extraList
    ) {
        if (!a_entry || !a_entry->extraLists || !a_extraList) {
            return false;
        }

        return std::ranges::find(*a_entry->extraLists, a_extraList) != a_entry->extraLists->end();
    }

    [[nodiscard]] RE::ExtraDataList* FindActorOwnedExtraList(
        RE::InventoryEntryData const* a_actorEntry,
        const RE::ExtraDataList* a_candidate
    ) {
        if (!a_actorEntry || !a_actorEntry->extraLists || !a_candidate) {
            return nullptr;
        }

        for (auto* extraList : *a_actorEntry->extraLists) {
            if (extraList == a_candidate) {
                return extraList;
            }
        }

        return nullptr;
    }

    [[nodiscard]] std::int32_t ExtraListCopyCount(const RE::ExtraDataList* a_extraList) {
        return a_extraList ? std::max(a_extraList->GetCount(), 1) : 0;
    }

    [[nodiscard]] bool IsOutfitManagedCopy(const RE::Actor& a_actor, const RE::ExtraDataList* a_extraList) {
        if (a_actor.IsPlayerRef()) {
            return false;
        }

        const auto* outfitItem = a_extraList ? a_extraList->GetByType<RE::ExtraOutfitItem>() : nullptr;
        return outfitItem != nullptr && outfitItem->id != 0;
    }

    [[nodiscard]] std::int32_t CountCustomCopies(const RE::InventoryEntryData* a_entry) {
        if (!a_entry || !a_entry->extraLists) {
            return 0;
        }

        auto count = std::int32_t {0};
        for (auto const* extraList : *a_entry->extraLists) {
            if (Inventory::HasCustomEnchantment(extraList)) {
                count += ExtraListCopyCount(extraList);
            }
        }
        return count;
    }

    [[nodiscard]] std::int32_t CountReservedOutfitManagedFormOnlyCopies(
        const RE::Actor& a_actor,
        const RE::InventoryEntryData* a_entry
    ) {
        if (!a_entry || !a_entry->extraLists) {
            return 0;
        }

        auto reserved = std::int32_t {0};
        for (auto const* extraList : *a_entry->extraLists) {
            if (!extraList || Inventory::HasCustomEnchantment(extraList)) {
                continue;
            }

            if (!IsOutfitManagedCopy(a_actor, extraList)) {
                continue;
            }

            const auto copyCount = ExtraListCopyCount(extraList);
            if (HasRightWornFlag(extraList)) {
                reserved += std::max(copyCount - 1, 0);
            } else {
                reserved += copyCount;
            }
        }

        return reserved;
    }

    [[nodiscard]] RightWornRing MakeRightWornRing(RE::TESObjectARMO& a_ring, RE::ExtraDataList* a_extraList) {
        return RightWornRing {
            .ring = std::addressof(a_ring),
            .extraList = a_extraList,
            .protectedStack = IsUnequipProtectedRingStack(a_extraList),
        };
    }

    [[nodiscard]] std::optional<RightWornRing> FindProtectedRightWornRingInExtraLists(
        RE::TESObjectARMO& a_ring,
        RE::InventoryEntryData const& a_entry,
        std::optional<RightWornRing>& a_firstRightWorn
    ) {
        if (!a_entry.extraLists) {
            return std::nullopt;
        }

        for (auto* extraList : *a_entry.extraLists) {
            if (!HasRightWornFlag(extraList)) {
                continue;
            }

            auto rightWorn = MakeRightWornRing(a_ring, extraList);
            if (rightWorn.protectedStack) {
                return rightWorn;
            }

            if (!a_firstRightWorn) {
                a_firstRightWorn = rightWorn;
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] bool HasRingArmorAddon(const RE::TESObjectARMO& a_armor) {
        return std::ranges::any_of(a_armor.armorAddons, [](const RE::TESObjectARMA* a_addon) {
            return a_addon && a_addon->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kRing);
        });
    }

    [[nodiscard]] bool IsClothingRingWithRingModel(const RE::TESObjectARMO& a_armor) {
        if (!a_armor.HasKeywordString(kClothingRingKeyword)) {
            return false;
        }

        return HasRingArmorAddon(a_armor) || SourceModelFootprints::HasRingModel(a_armor);
    }
}

std::uint64_t SelectionRevision(const RE::FormID a_actor) {
    std::scoped_lock const lock(selectionLock);
    const auto [entry, inserted] = SelectionRevisions().try_emplace(a_actor, 0);
    if (inserted) {
        entry->second = ++nextSelectionRevision;
    }
    return entry->second;
}

void InvalidateSelections(const RE::FormID a_actor) {
    std::scoped_lock const lock(selectionLock);
    if (const auto entry = SelectionRevisions().find(a_actor); entry != SelectionRevisions().end()) {
        entry->second = ++nextSelectionRevision;
    }
}

void RevertSelections() {
    std::scoped_lock const lock(selectionLock);
    SelectionRevisions().clear();
}

bool CustomSourceMatch::HasMatch() const {
    return firstExtraList != nullptr && count > 0;
}

bool SourceMatch::HasMatch() const {
    return count > 0;
}

bool HasCustomEnchantment(const RE::ExtraDataList* a_extraList) {
    const auto* enchantment = a_extraList ? a_extraList->GetByType<RE::ExtraEnchantment>() : nullptr;
    return enchantment != nullptr && enchantment->enchantment != nullptr;
}

namespace {
    std::optional<Core::CustomEnchantmentSignature> ReadCustomEnchantmentSignature(
        const RE::ExtraDataList* a_extraList
    ) {
        const auto* enchantment = a_extraList ? a_extraList->GetByType<RE::ExtraEnchantment>() : nullptr;
        if (!enchantment || !enchantment->enchantment) {
            return std::nullopt;
        }

        Core::CustomEnchantmentSignature signature {
            .enchantmentFormID = enchantment->enchantment->GetFormID(),
            .charge = enchantment->charge,
            .removeOnUnequip = enchantment->removeOnUnequip,
            .playerDisplayName = {},
        };

        if (auto displayName = ReadPlayerDisplayName(*a_extraList)) {
            signature.playerDisplayName = *displayName;
        }

        return signature;
    }

    std::optional<Core::ExtraUniqueIDKey> ReadExtraUniqueIDKey(const RE::ExtraDataList* a_extraList) {
        const auto* extraUniqueID = a_extraList ? a_extraList->GetByType<RE::ExtraUniqueID>() : nullptr;
        if (!extraUniqueID) {
            return std::nullopt;
        }

        const auto uniqueID = Core::ExtraUniqueIDKey {
            .baseID = extraUniqueID->baseID,
            .uniqueID = extraUniqueID->uniqueID,
        };
        return uniqueID.IsValid() ? std::make_optional(uniqueID) : std::nullopt;
    }

}

std::optional<Core::ExtraUniqueIDKey> EnsureExtraUniqueIDKey(
    RE::Actor& a_actor,
    const RE::TESBoundObject& a_object,
    RE::ExtraDataList& a_extraList
) {
    if (const auto existing = ReadExtraUniqueIDKey(std::addressof(a_extraList))) {
        return existing;
    }

    auto* inventoryChanges = a_actor.GetInventoryChanges();
    if (!inventoryChanges) {
        SKSE::log::warn(
            "Inventory: unique id assign skipped | form={:08X} | extraList={} | reason=noInventoryChanges",
            a_object.GetFormID(),
            static_cast<const void*>(std::addressof(a_extraList))
        );
        return std::nullopt;
    }

    auto const* entry = FindEntry(a_actor, a_object);
    if (!EntryContainsExtraList(entry, std::addressof(a_extraList))) {
        SKSE::log::warn(
            "Inventory: unique id assign skipped | form={:08X} | extraList={} | reason=extraListNotInInventory",
            a_object.GetFormID(),
            static_cast<const void*>(std::addressof(a_extraList))
        );
        return std::nullopt;
    }

    const auto uniqueID = inventoryChanges->GetNextUniqueID();
    if (uniqueID == 0) {
        SKSE::log::warn(
            "Inventory: unique id assign skipped | form={:08X} | extraList={} | reason=noUniqueID",
            a_object.GetFormID(),
            static_cast<const void*>(std::addressof(a_extraList))
        );
        return std::nullopt;
    }

    const auto uniqueBaseID = a_actor.GetFormID();
    if (RetainsInventoryIdentity(a_object) && !a_extraList.HasType<RE::ExtraReferenceHandle>()) {
        using SetUniqueID = void (*)(RE::InventoryChanges*, RE::ExtraDataList*, const RE::TESForm*, const RE::TESForm*);
        // CommonLib's AE SetUniqueID relocation points to SendContainerChangedEvent.
        static const REL::Relocation<SetUniqueID> setUniqueID {REL::VariantID(15907, 16147, 0x1FD7D0)};
        setUniqueID(inventoryChanges, std::addressof(a_extraList), nullptr, std::addressof(a_object));
        inventoryChanges->changed = true;
        return ReadExtraUniqueIDKey(std::addressof(a_extraList));
    }
    auto* extraUniqueID = new RE::ExtraUniqueID(uniqueBaseID, uniqueID);
    if (!a_extraList.Add(extraUniqueID)) {
        delete extraUniqueID;
        SKSE::log::warn(
            "Inventory: unique id assign skipped | form={:08X} | extraList={} | uniqueBase={:08X} | uniqueID={} | reason=addFailed",
            a_object.GetFormID(),
            static_cast<const void*>(std::addressof(a_extraList)),
            uniqueBaseID,
            uniqueID
        );
        return std::nullopt;
    }

    inventoryChanges->changed = true;
    return ReadExtraUniqueIDKey(std::addressof(a_extraList));
}

namespace {
    bool MatchesCustomEnchantmentSignature(
        const RE::ExtraDataList* a_extraList,
        const Core::CustomEnchantmentSignature& a_signature
    ) {
        const auto* extra = a_extraList ? a_extraList->GetByType<RE::ExtraEnchantment>() : nullptr;
        return extra
               != nullptr
               && extra->enchantment
               != nullptr
               && extra->enchantment->GetFormID()
               == a_signature.enchantmentFormID
               && extra->charge
               == a_signature.charge
               && extra->removeOnUnequip
               == a_signature.removeOnUnequip
               && ReadPlayerDisplayName(*a_extraList).value_or(std::string_view {})
               == a_signature.playerDisplayName;
    }

    bool MatchesExtraUniqueIDKey(const RE::ExtraDataList* a_extraList, const Core::ExtraUniqueIDKey& a_uniqueID) {
        const auto uniqueID = ReadExtraUniqueIDKey(a_extraList);
        return uniqueID && *uniqueID == a_uniqueID;
    }
}

bool MatchesCustomSelection(
    const RE::ExtraDataList* a_extraList,
    const Core::CustomEnchantmentSignature& a_signature,
    const std::optional<Core::ExtraUniqueIDKey>& a_uniqueID
) {
    if (!MatchesCustomEnchantmentSignature(a_extraList, a_signature)) {
        return false;
    }

    return !a_uniqueID || MatchesExtraUniqueIDKey(a_extraList, *a_uniqueID);
}

bool MatchesSource(const RE::ExtraDataList* a_extraList, const Core::ItemSource& a_source) {
    if (a_source.IsCustomEnchantment()) {
        return MatchesCustomSelection(a_extraList, a_source.customEnchantment, a_source.extraUniqueID);
    }
    return a_source.IsFormOnly()
           && !HasCustomEnchantment(a_extraList)
           && (!a_source.extraUniqueID || ReadExtraUniqueIDKey(a_extraList) == a_source.extraUniqueID);
}

SourceMatch FindSourceMatches(RE::Actor& a_actor, const Core::ItemSource& a_source) {
    auto const* ring = AsRing(RE::TESForm::LookupByID(a_source.sourceFormID));
    if (!ring) {
        return {};
    }
    if (a_source.IsFormOnly() && !a_source.extraUniqueID) {
        return FindFormOnlySourceMatches(a_actor, *ring);
    }
    SourceMatch result;
    const auto* entry = FindEntry(a_actor, *ring);
    if (!entry || !entry->extraLists) {
        return result;
    }
    for (auto* extraList : *entry->extraLists) {
        if (!extraList || !MatchesSource(extraList, a_source) || IsOutfitManagedCopy(a_actor, extraList)) {
            continue;
        }
        result.count += ExtraListCopyCount(extraList);
        if (!result.firstExtraList) {
            result.firstExtraList = extraList;
        }
        if (HasRightWornFlag(extraList)) {
            result.rightWorn = true;
            result.rightWornExtraList = extraList;
            result.rightWornProtected = IsUnequipProtectedRingStack(extraList);
            result.firstExtraList = extraList;
        }
    }
    return result;
}

namespace {
    struct AvailableCopy {
        RE::ExtraDataList* extraList {nullptr};
        std::int32_t represented {0};
    };

    AvailableCopy FindAvailableCopy(
        const RE::Actor& a_actor,
        const RE::InventoryEntryData* a_entry,
        const Core::ItemSource& a_source,
        const std::span<const Core::ItemSource> a_claimed,
        const bool a_untrackedOnly
    ) {
        AvailableCopy result;
        if (!a_entry || !a_entry->extraLists) {
            return result;
        }
        for (auto* extraList : *a_entry->extraLists) {
            result.represented += ExtraListCopyCount(extraList);
            if (!extraList
                || HasRightWornFlag(extraList)
                || IsOutfitManagedCopy(a_actor, extraList)
                || !MatchesSource(extraList, a_source)) {
                continue;
            }
            // DeepCopy preserves ExtraReferenceHandle, so split copies would share the original reference.
            if (ExtraListCopyCount(extraList) > 1 && extraList->HasType<RE::ExtraReferenceHandle>()) {
                continue;
            }
            auto candidate = a_source;
            candidate.extraUniqueID = ReadExtraUniqueIDKey(extraList);
            if ((a_untrackedOnly && candidate.extraUniqueID)
                || std::ranges::any_of(a_claimed, [&](const auto& a_claim) { return a_claim.IsSameCopy(candidate); })) {
                continue;
            }
            result.extraList = extraList;
            break;
        }
        return result;
    }

    std::optional<Core::ExtraUniqueIDKey> PrepareCopyIdentity(
        RE::Actor& a_actor,
        const RE::TESObjectARMO& a_ring,
        RE::ExtraDataList& a_extraList,
        const std::span<const Core::ItemSource> a_claimed
    ) {
        const auto identity = ReadExtraUniqueIDKey(std::addressof(a_extraList));
        if (!identity) {
            return EnsureExtraUniqueIDKey(a_actor, a_ring, a_extraList);
        }
        const auto* entry = FindEntry(a_actor, a_ring);
        const auto duplicate = std::ranges::any_of(*entry->extraLists, [&](const auto* a_other) {
            return a_other != std::addressof(a_extraList) && ReadExtraUniqueIDKey(a_other) == identity;
        });
        if (!duplicate) {
            return identity;
        }
        const auto claimed = std::ranges::any_of(a_claimed, [&](const auto& a_source) {
            return a_source.sourceFormID == a_ring.GetFormID() && a_source.extraUniqueID == identity;
        });
        if (claimed || RetainsInventoryIdentity(a_ring) || a_extraList.HasType<RE::ExtraReferenceHandle>()) {
            return std::nullopt;
        }
        auto* changes = a_actor.GetInventoryChanges();
        const auto next = changes->GetNextUniqueID();
        if (next == 0) {
            return std::nullopt;
        }
        auto* uniqueID = a_extraList.GetByType<RE::ExtraUniqueID>();
        uniqueID->baseID = a_actor.GetFormID();
        uniqueID->uniqueID = next;
        changes->changed = true;
        return ReadExtraUniqueIDKey(std::addressof(a_extraList));
    }

    void SplitCopy(RE::TESObjectARMO& a_ring, RE::InventoryEntryData& a_entry, RE::ExtraDataList& a_selected) {
        const auto count = ExtraListCopyCount(std::addressof(a_selected));
        if (count <= 1) {
            return;
        }
        RE::InventoryEntryData original(std::addressof(a_ring), 0);
        original.AddExtraList(std::addressof(a_selected));
        RE::InventoryEntryData copy(std::addressof(a_ring), 0);
        copy.DeepCopy(original);
        auto* remainder = copy.extraLists->front();
        remainder->RemoveByType(RE::ExtraDataType::kUniqueID);
        remainder->SetCount(static_cast<std::uint16_t>(count - 1));
        a_selected.SetCount(1);
        a_entry.AddExtraList(remainder);
    }
}

std::optional<Core::ItemSource> AcquireCopy(
    RE::Actor& a_actor,
    const Core::ItemSource& a_source,
    const std::span<const Core::ItemSource> a_claimed,
    const bool a_untrackedOnly
) {
    auto* ring = AsRing(RE::TESForm::LookupByID(a_source.sourceFormID));
    auto* changes = a_actor.GetInventoryChanges();
    if (!ring || !changes || GetCount(a_actor, *ring) <= 0) {
        return std::nullopt;
    }
    auto* entry = FindEntry(a_actor, *ring);
    const auto available = FindAvailableCopy(a_actor, entry, a_source, a_claimed, a_untrackedOnly);
    auto* selected = available.extraList;
    if (!selected
        && (!a_source.IsFormOnly() || a_source.extraUniqueID || available.represented >= GetCount(a_actor, *ring))) {
        return std::nullopt;
    }
    if (!entry) {
        entry = new RE::InventoryEntryData(ring, 0);
        changes->AddEntryData(entry);
    }
    if (!selected) {
        using Construct = RE::ExtraDataList* (*)(void*);
        static const REL::Relocation<Construct> construct {REL::VariantID(11437, 11583, 0x117C80)};
        selected = construct(RE::malloc(REL::Relocate<std::size_t>(0x18, 0x20, 0x18)));
        entry->AddExtraList(selected);
    }
    const auto identity = PrepareCopyIdentity(a_actor, *ring, *selected, a_claimed);
    if (!identity) {
        return std::nullopt;
    }
    SplitCopy(*ring, *entry, *selected);
    changes->changed = true;
    auto result = a_source;
    result.extraUniqueID = identity;
    return result;
}

bool IsUnequipProtectedRingStack(RE::ExtraDataList* a_extraList) {
    if (!a_extraList) {
        return false;
    }

    if (a_extraList->HasQuestObjectAlias()) {
        return true;
    }

    return a_extraList->HasType<RE::ExtraCannotWear>();
}

bool HasRightWornFlag(const RE::ExtraDataList* a_extraList) {
    return a_extraList != nullptr && a_extraList->HasType<RE::ExtraWorn>();
}

bool IsRingSourceRightWorn(RE::Actor& a_actor, const RE::TESObjectARMO& a_ring, const RE::ExtraDataList* a_extraList) {
    auto const* entry = FindEntry(a_actor, a_ring);
    if (!entry) {
        return false;
    }

    if (a_extraList) {
        return EntryContainsExtraList(entry, a_extraList) && HasRightWornFlag(a_extraList);
    }

    return IsFormOnlyRightWorn(*entry);
}

bool RightWornRingMatchesSource(
    const RightWornRing& a_rightWorn,
    const RE::TESObjectARMO& a_ring,
    const Core::ItemSource& a_source
) {
    if (!a_rightWorn.ring || a_rightWorn.ring->GetFormID() != a_ring.GetFormID()) {
        return false;
    }

    if (a_source.IsFormOnly()) {
        return MatchesSource(a_rightWorn.extraList, a_source);
    }

    return a_source.IsCustomEnchantment()
           && MatchesCustomSelection(a_rightWorn.extraList, a_source.customEnchantment, a_source.extraUniqueID);
}

std::optional<RightWornRing> FindRightWornRing(RE::Actor& a_actor) {
    auto const* inventoryChanges = a_actor.GetInventoryChanges();
    if (!inventoryChanges || !inventoryChanges->entryList) {
        return std::nullopt;
    }

    std::optional<RightWornRing> firstRightWorn;
    for (auto const* entry : *inventoryChanges->entryList) {
        auto* ring = entry ? AsRing(entry->object) : nullptr;
        if (!ring) {
            continue;
        }

        if (entry->extraLists) {
            if (auto protectedRightWorn = FindProtectedRightWornRingInExtraLists(*ring, *entry, firstRightWorn)) {
                return protectedRightWorn;
            }
            continue;
        }

        if (entry->IsWorn(false) && !firstRightWorn) {
            firstRightWorn = MakeRightWornRing(*ring, nullptr);
        }
    }

    return firstRightWorn;
}

bool HasProtectedRightWornRing(RE::Actor& a_actor) {
    const auto rightWorn = FindRightWornRing(a_actor);
    return rightWorn && rightWorn->protectedStack;
}

RightWornRingUnequipResult UnequipRightWornRing(RE::Actor& a_actor) {
    auto unequippedAny = false;
    for (auto attempt = std::uint8_t {0}; attempt < kMaxRightWornRingUnequipAttempts; ++attempt) {
        const auto rightWorn = FindRightWornRing(a_actor);
        if (!rightWorn) {
            return unequippedAny ? RightWornRingUnequipResult::kUnequipped : RightWornRingUnequipResult::kNone;
        }

        if (!rightWorn->ring) {
            return RightWornRingUnequipResult::kFailed;
        }

        if (rightWorn->protectedStack) {
            return RightWornRingUnequipResult::kProtected;
        }

        auto* equipManager = RE::ActorEquipManager::GetSingleton();
        if (!equipManager) {
            return RightWornRingUnequipResult::kFailed;
        }

        auto const* unequippedRing = rightWorn->ring;
        auto const* unequippedExtraList = rightWorn->extraList;
        equipManager->UnequipObject(
            std::addressof(a_actor),
            rightWorn->ring,
            rightWorn->extraList,
            1,
            nullptr,
            true,
            false,
            false,
            true,
            nullptr
        );

        unequippedAny = true;
        const auto nextRightWorn = FindRightWornRing(a_actor);
        if (nextRightWorn && nextRightWorn->ring == unequippedRing && nextRightWorn->extraList == unequippedExtraList) {
            return RightWornRingUnequipResult::kFailed;
        }
    }

    return FindRightWornRing(a_actor).has_value() ? RightWornRingUnequipResult::kFailed
                                                  : RightWornRingUnequipResult::kUnequipped;
}

std::optional<std::string_view> ReadPlayerDisplayName(const RE::ExtraDataList& a_extraList) {
    const auto* displayName = a_extraList.GetByType<RE::ExtraTextDisplayData>();
    if (!displayName || !displayName->IsPlayerSet() || displayName->displayName.empty()) {
        return std::nullopt;
    }

    return displayName->displayName.c_str();
}

RE::InventoryEntryData* FindEntry(RE::Actor& a_actor, const RE::TESBoundObject& a_object) {
    auto const* inventoryChanges = a_actor.GetInventoryChanges();
    if (!inventoryChanges || !inventoryChanges->entryList) {
        return nullptr;
    }

    for (auto* entry : *inventoryChanges->entryList) {
        if (entry && entry->object == std::addressof(a_object)) {
            return entry;
        }
    }

    return nullptr;
}

std::int32_t GetCount(RE::Actor& a_actor, const RE::TESBoundObject& a_object) {
    auto const* inventoryChanges = a_actor.GetInventoryChanges();
    if (!inventoryChanges) {
        return 0;
    }

    return inventoryChanges->GetCount(std::addressof(a_object), [](const RE::InventoryEntryData*) { return true; });
}

namespace {
    CustomSourceMatch FindCustomMatches(
        RE::InventoryEntryData const* a_entry,
        const Core::CustomEnchantmentSignature& a_signature,
        const std::optional<Core::ExtraUniqueIDKey>& a_uniqueID
    ) {
        CustomSourceMatch state;
        if (!a_entry || !a_entry->extraLists) {
            return state;
        }

        for (auto* extraList : *a_entry->extraLists) {
            if (!MatchesCustomSelection(extraList, a_signature, a_uniqueID)) {
                continue;
            }

            if (!state.firstExtraList) {
                state.firstExtraList = extraList;
            }

            state.count += std::max(extraList->GetCount(), 1);
            if (HasRightWornFlag(extraList)) {
                state.rightWornExtraList = extraList;
                state.firstExtraList = extraList;
                state.rightWornProtected = IsUnequipProtectedRingStack(extraList);
            }
        }

        return state;
    }
}

CustomSourceMatch FindCustomSourceMatches(
    RE::Actor& a_actor,
    const RE::TESObjectARMO& a_ring,
    const Core::CustomEnchantmentSignature& a_signature,
    const std::optional<Core::ExtraUniqueIDKey>& a_uniqueID
) {
    return FindCustomMatches(FindEntry(a_actor, a_ring), a_signature, a_uniqueID);
}

SourceMatch FindFormOnlySourceMatches(RE::Actor& a_actor, const RE::TESObjectARMO& a_ring) {
    auto const* entry = FindEntry(a_actor, a_ring);
    const auto totalCount = GetCount(a_actor, a_ring);
    const auto customCount = CountCustomCopies(entry);
    const auto reservedOutfitCount = CountReservedOutfitManagedFormOnlyCopies(a_actor, entry);
    const auto formOnlyCount = std::max(totalCount - customCount - reservedOutfitCount, 0);
    auto* rightWornExtraList = FindRightWornFormOnlyExtraList(entry);

    SourceMatch state {
        .firstExtraList = rightWornExtraList,
        .rightWornExtraList = rightWornExtraList,
        .count = formOnlyCount,
        .rightWornProtected = IsUnequipProtectedRingStack(rightWornExtraList),
        .rightWorn = rightWornExtraList
                     != nullptr
                     || (entry != nullptr && entry->extraLists == nullptr && entry->IsWorn(false)),
    };

    if (!state.firstExtraList && entry && entry->extraLists) {
        for (auto* extraList : *entry->extraLists) {
            if (extraList && !HasCustomEnchantment(extraList) && !IsOutfitManagedCopy(a_actor, extraList)) {
                state.firstExtraList = extraList;
                break;
            }
        }
    }

    return state;
}

namespace {
    bool ResolveCustomSelectionFromExtraList(EntryCustomSelection& a_selection, RE::ExtraDataList* a_extraList) {
        const auto signature = ReadCustomEnchantmentSignature(a_extraList);
        if (!signature) {
            return true;
        }

        if (a_selection.signature && *a_selection.signature != *signature) {
            a_selection.failure = EntryCustomFailure::kMultipleCustomEnchantments;
            a_selection.extraList = nullptr;
            a_selection.signature = std::nullopt;
            a_selection.uniqueID = std::nullopt;
            return false;
        }

        a_selection.signature = signature;
        if (!a_selection.extraList || HasRightWornFlag(a_extraList)) {
            a_selection.extraList = a_extraList;
            a_selection.uniqueID = ReadExtraUniqueIDKey(a_extraList);
        }

        return true;
    }

    EntryCustomSelection ResolveEntryCustomSelection(RE::InventoryEntryData const& a_entry) {
        EntryCustomSelection selection;
        if (!a_entry.extraLists) {
            return selection;
        }

        for (auto* extraList : *a_entry.extraLists) {
            if (!ResolveCustomSelectionFromExtraList(selection, extraList)) {
                return selection;
            }
        }

        return selection;
    }

    MenuEntryExtraListResolution ResolveMenuEntryExtraLists(
        RE::InventoryEntryData const& a_menuEntry,
        RE::InventoryEntryData const* a_actorEntry
    ) {
        MenuEntryExtraListResolution resolution;
        // Plain menu stacks can have an allocated but empty extra-data list.
        if (!a_menuEntry.extraLists || a_menuEntry.extraLists->empty()) {
            resolution.hasFormOnlySource = true;
            resolution.formOnlyRightWorn = a_menuEntry.IsWorn(false);
            return resolution;
        }

        for (const auto* candidate : *a_menuEntry.extraLists) {
            auto* extraList = FindActorOwnedExtraList(a_actorEntry, candidate);
            if (!extraList) {
                continue;
            }

            if (HasCustomEnchantment(extraList)) {
                if (!ResolveCustomSelectionFromExtraList(resolution.customSelection, extraList)) {
                    return resolution;
                }
                continue;
            }

            resolution.hasFormOnlySource = true;
            resolution.formOnlyRightWorn = resolution.formOnlyRightWorn || HasRightWornFlag(extraList);
        }

        return resolution;
    }

    EntryRingSource MakeFormOnlyEntryRingSource(RE::TESObjectARMO& a_ring, const bool a_rightWorn) {
        return EntryRingSource {
            .ring = std::addressof(a_ring),
            .source = Core::ItemSource {
                .kind = Core::ItemSourceKind::kFormOnly,
                .sourceFormID = a_ring.GetFormID(),
            },
            .vanillaRingSlotEquipped = a_rightWorn,
        };
    }

    std::optional<EntryRingSource> ResolveActorInventoryEntryRingSource(
        RE::InventoryEntryData const& a_entry,
        RE::TESObjectARMO& a_ring
    ) {
        auto source = MakeFormOnlyEntryRingSource(a_ring, IsFormOnlyRightWorn(a_entry));

        auto customSelection = ResolveEntryCustomSelection(a_entry);
        source.customFailure = customSelection.failure;
        if (source.customFailure != EntryCustomFailure::kNone) {
            source.source = {};
            return source;
        }

        if (!customSelection.extraList || !customSelection.signature) {
            return source;
        }

        source.source = Core::ItemSource {
            .kind = Core::ItemSourceKind::kCustomEnchantment,
            .sourceFormID = a_ring.GetFormID(),
            .customEnchantment = *customSelection.signature,
            .extraUniqueID = customSelection.uniqueID,
        };
        source.vanillaRingSlotEquipped = HasRightWornFlag(customSelection.extraList);
        return source;
    }

    std::optional<EntryRingSource> ResolveMenuEntryRingSource(
        RE::Actor& a_actor,
        RE::InventoryEntryData const& a_entry,
        RE::TESObjectARMO& a_ring
    ) {
        auto const* actorEntry = FindEntry(a_actor, a_ring);
        auto menuExtraLists = ResolveMenuEntryExtraLists(a_entry, actorEntry);
        auto source = MakeFormOnlyEntryRingSource(a_ring, menuExtraLists.formOnlyRightWorn);

        source.customFailure = menuExtraLists.customSelection.failure;
        if (source.customFailure != EntryCustomFailure::kNone) {
            source.source = {};
            return source;
        }

        if (!menuExtraLists.customSelection.extraList || !menuExtraLists.customSelection.signature) {
            if (menuExtraLists.hasFormOnlySource) {
                return source;
            }

            const auto actorFormOnlySource = FindFormOnlySourceMatches(a_actor, a_ring);
            if (actorFormOnlySource.HasMatch() && CountCustomCopies(actorEntry) == 0) {
                return MakeFormOnlyEntryRingSource(a_ring, actorFormOnlySource.rightWorn);
            }

            return std::nullopt;
        }

        auto& customSelection = menuExtraLists.customSelection;
        source.source = Core::ItemSource {
            .kind = Core::ItemSourceKind::kCustomEnchantment,
            .sourceFormID = a_ring.GetFormID(),
            .customEnchantment = *customSelection.signature,
            .extraUniqueID = customSelection.uniqueID,
        };
        source.vanillaRingSlotEquipped = HasRightWornFlag(customSelection.extraList);
        return source;
    }
    void CollectMenuRowSources(RE::Actor& a_actor, const RE::InventoryEntryData& a_entry, EntryRingSource& a_result) {
        const auto* actorEntry = FindEntry(a_actor, *a_result.ring);
        auto represented = 0;
        if (a_entry.extraLists) {
            for (const auto* candidate : *a_entry.extraLists) {
                const auto* extraList = FindActorOwnedExtraList(actorEntry, candidate);
                if (!extraList) {
                    continue;
                }
                represented += ExtraListCopyCount(extraList);
                Core::ItemSource source {
                    .kind = Core::ItemSourceKind::kFormOnly,
                    .sourceFormID = a_result.ring->GetFormID(),
                };
                if (const auto signature = ReadCustomEnchantmentSignature(extraList)) {
                    source.kind = Core::ItemSourceKind::kCustomEnchantment;
                    source.customEnchantment = *signature;
                }
                source.extraUniqueID = ReadExtraUniqueIDKey(extraList);
                a_result.rowSources.push_back(std::move(source));
            }
        }
        if (a_entry.countDelta > represented) {
            a_result.rowSources.push_back(
                {.kind = Core::ItemSourceKind::kFormOnly, .sourceFormID = a_result.ring->GetFormID()}
            );
        }
    }
}

std::optional<EntryRingSource> ResolveEntryRingSource(
    RE::Actor& a_actor,
    RE::InventoryEntryData& a_entry,
    const EntryResolveScope a_scope
) {
    auto* ring = AsRing(a_entry.GetObject());
    if (!ring || GetCount(a_actor, *ring) <= 0) {
        return std::nullopt;
    }

    if (a_scope == EntryResolveScope::kMenuRow) {
        auto result = ResolveMenuEntryRingSource(a_actor, a_entry, *ring);
        if (!result || result->customFailure != EntryCustomFailure::kNone) {
            return result;
        }
        CollectMenuRowSources(a_actor, a_entry, *result);
        return result;
    }

    return ResolveActorInventoryEntryRingSource(a_entry, *ring);
}

std::optional<EntryRingSource> PrepareMenuRingSelection(
    RE::Actor& a_actor,
    RE::InventoryEntryData& a_entry,
    const std::span<const Core::ItemSource> a_claimed
) {
    auto const* ring = AsRing(a_entry.object);
    if (!ring) {
        return std::nullopt;
    }
    const auto* owned = FindEntry(a_actor, *ring);
    if (a_entry.extraLists && owned && owned->extraLists) {
        for (const auto* candidate : *a_entry.extraLists) {
            auto* extraList = FindActorOwnedExtraList(owned, candidate);
            if (!extraList) {
                continue;
            }
            if (!PrepareCopyIdentity(a_actor, *ring, *extraList, a_claimed)) {
                return std::nullopt;
            }
        }
    }
    return ResolveEntryRingSource(a_actor, a_entry, EntryResolveScope::kMenuRow);
}

RE::TESObjectARMO* AsRing(RE::TESBoundObject* a_object) {
    if (!a_object) {
        return nullptr;
    }

    auto* armor = a_object->As<RE::TESObjectARMO>();
    return IsRing(armor) ? armor : nullptr;
}

RE::TESObjectARMO* AsRing(RE::TESForm* a_form) {
    if (!a_form) {
        return nullptr;
    }

    auto* armor = a_form->As<RE::TESObjectARMO>();
    return IsRing(armor) ? armor : nullptr;
}

bool IsRing(const RE::TESObjectARMO* a_armor) {
    if (!a_armor || a_armor->armorAddons.empty()) {
        return false;
    }

    if (a_armor->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kRing)) {
        return Compatibility::Vanilla::IsOfficialRingDefiningFile(a_armor->GetFile(0))
               || SourceModelFootprints::HasRingModel(*a_armor);
    }

    return IsClothingRingWithRingModel(*a_armor);
}

bool HasClothingRingKeyword(const RE::TESObjectARMO* a_armor) {
    return a_armor != nullptr && a_armor->HasKeywordString(kClothingRingKeyword);
}

RingInventoryState GetRingInventoryState(RE::Actor& a_actor, const RE::TESObjectARMO& a_ring) {
    auto const* entry = Inventory::FindEntry(a_actor, a_ring);
    auto* rightWornExtraList = FindRightWornExtraList(entry);
    const auto rightWorn = entry != nullptr && entry->IsWorn(false);
    return RingInventoryState {
        .rightWornExtraList = rightWornExtraList,
        .rightWorn = rightWorn,
    };
}
}
