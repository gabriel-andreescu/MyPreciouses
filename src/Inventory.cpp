#include "Inventory.h"

#include "SourceModelFootprints.h"

#include <RE/E/ExtraCannotWear.h>
#include <RE/E/ExtraOutfitItem.h>

#include <algorithm>
#include <utility>

namespace Inventory {
namespace {
    constexpr auto kClothingRingKeyword = "ClothingRing"sv;
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

        [[nodiscard]] bool HasCustomEnchantment() const {
            return extraList != nullptr && signature.has_value() && failure == EntryCustomFailure::kNone;
        }
    };

    struct MenuEntryExtraListResolution {
        EntryCustomSelection customSelection;
        bool hasFormOnlySource {false};
        bool formOnlyRightWorn {false};
    };

    [[nodiscard]] std::optional<std::string> ReadPlayerDisplayName(const RE::ExtraDataList& a_extraList) {
        const auto* displayName = a_extraList.GetByType<RE::ExtraTextDisplayData>();
        if (!displayName || !displayName->IsPlayerSet() || displayName->displayName.c_str()[0] == '\0') {
            return std::nullopt;
        }

        return displayName->displayName.c_str();
    }

    [[nodiscard]] RE::ExtraDataList* FindRightWornExtraList(
        RE::InventoryEntryData* a_entry,
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

    [[nodiscard]] RE::ExtraDataList* FindRightWornExtraList(RE::InventoryEntryData* a_entry) {
        return FindRightWornExtraList(a_entry, RightWornExtraListFilter::kAny);
    }

    [[nodiscard]] RE::ExtraDataList* FindRightWornFormOnlyExtraList(RE::InventoryEntryData* a_entry) {
        return FindRightWornExtraList(a_entry, RightWornExtraListFilter::kFormOnly);
    }

    [[nodiscard]] bool IsFormOnlyRightWorn(RE::InventoryEntryData& a_entry) {
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
        RE::InventoryEntryData* a_actorEntry,
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

    [[nodiscard]] bool IsOutfitManagedCopy(const RE::ExtraDataList* a_extraList) {
        const auto* outfitItem = a_extraList ? a_extraList->GetByType<RE::ExtraOutfitItem>() : nullptr;
        return outfitItem && outfitItem->id != 0;
    }

    [[nodiscard]] std::int32_t CountCustomCopies(const RE::InventoryEntryData* a_entry) {
        if (!a_entry || !a_entry->extraLists) {
            return 0;
        }

        auto count = std::int32_t {0};
        for (auto* extraList : *a_entry->extraLists) {
            if (Inventory::HasCustomEnchantment(extraList)) {
                count += ExtraListCopyCount(extraList);
            }
        }
        return count;
    }

    [[nodiscard]] std::int32_t CountReservedOutfitManagedFormOnlyCopies(const RE::InventoryEntryData* a_entry) {
        if (!a_entry || !a_entry->extraLists) {
            return 0;
        }

        auto reserved = std::int32_t {0};
        for (auto* extraList : *a_entry->extraLists) {
            if (!extraList || Inventory::HasCustomEnchantment(extraList)) {
                continue;
            }

            if (!IsOutfitManagedCopy(extraList)) {
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
        RE::InventoryEntryData& a_entry,
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

    [[nodiscard]] bool IsClothingRingWithRingEvidence(const RE::TESObjectARMO& a_armor) {
        if (!a_armor.HasKeywordString(kClothingRingKeyword)) {
            return false;
        }

        return HasRingArmorAddon(a_armor) || SourceModelFootprints::HasRingModelEvidence(a_armor);
    }
}

bool CustomSourceMatch::HasMatch() const {
    return firstExtraList != nullptr && count > 0;
}

bool FormOnlySourceMatch::HasMatch() const {
    return count > 0;
}

bool HasCustomEnchantment(const RE::ExtraDataList* a_extraList) {
    const auto* enchantment = a_extraList ? a_extraList->GetByType<RE::ExtraEnchantment>() : nullptr;
    return enchantment && enchantment->enchantment;
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
        };

        if (auto displayName = ReadPlayerDisplayName(*a_extraList)) {
            signature.playerDisplayName = std::move(*displayName);
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

    std::optional<Core::ExtraUniqueIDKey> EnsureExtraUniqueIDKey(
        RE::Actor& a_actor,
        const RE::TESBoundObject& a_object,
        RE::ExtraDataList& a_extraList
    ) {
        if (auto existing = ReadExtraUniqueIDKey(std::addressof(a_extraList))) {
            return existing;
        }

        auto* inventoryChanges = a_actor.GetInventoryChanges();
        if (!inventoryChanges) {
            logger::warn(
                "Inventory: unique id assign skipped | form={:08X} | extraList={} | reason=noInventoryChanges",
                a_object.GetFormID(),
                static_cast<const void*>(std::addressof(a_extraList))
            );
            return std::nullopt;
        }

        auto* entry = FindEntry(a_actor, a_object);
        if (!EntryContainsExtraList(entry, std::addressof(a_extraList))) {
            logger::warn(
                "Inventory: unique id assign skipped | form={:08X} | extraList={} | reason=extraListNotInInventory",
                a_object.GetFormID(),
                static_cast<const void*>(std::addressof(a_extraList))
            );
            return std::nullopt;
        }

        const auto uniqueID = inventoryChanges->GetNextUniqueID();
        if (uniqueID == 0) {
            logger::warn(
                "Inventory: unique id assign skipped | form={:08X} | extraList={} | reason=noUniqueID",
                a_object.GetFormID(),
                static_cast<const void*>(std::addressof(a_extraList))
            );
            return std::nullopt;
        }

        const auto uniqueBaseID = a_actor.GetFormID();
        auto* extraUniqueID = new RE::ExtraUniqueID(uniqueBaseID, uniqueID);
        if (!a_extraList.Add(extraUniqueID)) {
            delete extraUniqueID;
            logger::warn(
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

    std::optional<Core::ExtraUniqueIDKey> EnsureEntryCustomSelectionUniqueID(
        RE::Actor& a_actor,
        const RE::TESBoundObject& a_object,
        EntryCustomSelection& a_customSelection
    ) {
        if (!a_customSelection.HasCustomEnchantment() || !a_customSelection.extraList) {
            return std::nullopt;
        }

        if (a_customSelection.uniqueID) {
            return a_customSelection.uniqueID;
        }

        a_customSelection.uniqueID = EnsureExtraUniqueIDKey(a_actor, a_object, *a_customSelection.extraList);
        return a_customSelection.uniqueID;
    }

    bool MatchesCustomEnchantmentSignature(
        const RE::ExtraDataList* a_extraList,
        const Core::CustomEnchantmentSignature& a_signature
    ) {
        const auto signature = ReadCustomEnchantmentSignature(a_extraList);
        return signature && *signature == a_signature;
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
    return a_extraList && a_extraList->HasType<RE::ExtraWorn>();
}

bool IsRingSourceRightWorn(RE::Actor& a_actor, const RE::TESObjectARMO& a_ring, const RE::ExtraDataList* a_extraList) {
    auto* entry = FindEntry(a_actor, a_ring);
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
        return !HasCustomEnchantment(a_rightWorn.extraList);
    }

    return a_source.IsCustomEnchantment()
           && MatchesCustomSelection(a_rightWorn.extraList, a_source.customEnchantment, a_source.extraUniqueID);
}

std::optional<RightWornRing> FindRightWornRing(RE::Actor& a_actor) {
    auto* inventoryChanges = a_actor.GetInventoryChanges();
    if (!inventoryChanges || !inventoryChanges->entryList) {
        return std::nullopt;
    }

    std::optional<RightWornRing> firstRightWorn;
    for (auto* entry : *inventoryChanges->entryList) {
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

        auto* unequippedRing = rightWorn->ring;
        auto* unequippedExtraList = rightWorn->extraList;
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

std::optional<CustomEnchantmentData> ReadCustomEnchantment(const RE::ExtraDataList& a_extraList) {
    const auto* enchantment = a_extraList.GetByType<RE::ExtraEnchantment>();
    if (!enchantment || !enchantment->enchantment) {
        return std::nullopt;
    }

    CustomEnchantmentData data {
        .enchantment = enchantment->enchantment,
        .charge = enchantment->charge,
        .removeOnUnequip = enchantment->removeOnUnequip,
    };

    data.playerDisplayName = ReadPlayerDisplayName(a_extraList);

    return data;
}

bool MirrorCustomEnchantment(RE::ExtraDataList& a_target, const RE::ExtraDataList& a_source) {
    const auto customData = ReadCustomEnchantment(a_source);
    if (!customData) {
        return false;
    }

    a_target.SetEnchantment(nullptr, 0, false);
    a_target.SetEnchantment(customData->enchantment, customData->charge, customData->removeOnUnequip);

    if (customData->playerDisplayName) {
        a_target.SetOverrideName(customData->playerDisplayName->c_str());
    }
    return true;
}

RE::InventoryEntryData* FindEntry(RE::Actor& a_actor, const RE::TESBoundObject& a_object) {
    auto* inventoryChanges = a_actor.GetInventoryChanges();
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
    auto* inventoryChanges = a_actor.GetInventoryChanges();
    if (!inventoryChanges) {
        return 0;
    }

    return inventoryChanges->GetCount(std::addressof(a_object), [](const RE::InventoryEntryData*) {
        return true;
    });
}

namespace {
    CustomSourceMatch FindCustomMatches(
        RE::InventoryEntryData* a_entry,
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

FormOnlySourceMatch FindFormOnlySourceMatches(RE::Actor& a_actor, const RE::TESObjectARMO& a_ring) {
    auto* entry = FindEntry(a_actor, a_ring);
    const auto totalCount = GetCount(a_actor, a_ring);
    const auto customCount = CountCustomCopies(entry);
    const auto reservedOutfitCount = CountReservedOutfitManagedFormOnlyCopies(entry);
    const auto formOnlyCount = std::max(totalCount - customCount - reservedOutfitCount, 0);
    auto* rightWornExtraList = FindRightWornFormOnlyExtraList(entry);

    FormOnlySourceMatch state {
        .rightWornExtraList = rightWornExtraList,
        .count = formOnlyCount,
        .rightWornProtected = IsUnequipProtectedRingStack(rightWornExtraList),
        .rightWorn = rightWornExtraList != nullptr || (entry && !entry->extraLists && entry->IsWorn(false)),
    };

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

    EntryCustomSelection ResolveEntryCustomSelection(RE::InventoryEntryData& a_entry) {
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
        RE::InventoryEntryData& a_menuEntry,
        RE::InventoryEntryData* a_actorEntry
    ) {
        MenuEntryExtraListResolution resolution;
        if (!a_menuEntry.extraLists) {
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
        RE::Actor& a_actor,
        RE::InventoryEntryData& a_entry,
        RE::TESObjectARMO& a_ring,
        const SourceResolveMode a_mode
    ) {
        auto source = MakeFormOnlyEntryRingSource(a_ring, IsFormOnlyRightWorn(a_entry));

        auto customSelection = ResolveEntryCustomSelection(a_entry);
        source.customFailure = customSelection.failure;
        if (source.customFailure != EntryCustomFailure::kNone) {
            source.source = {};
            return source;
        }

        if (!customSelection.HasCustomEnchantment()) {
            return source;
        }

        const auto extraUniqueID = a_mode == SourceResolveMode::kEnsureCustomUniqueID
                                       ? EnsureEntryCustomSelectionUniqueID(a_actor, a_ring, customSelection)
                                       : customSelection.uniqueID;
        if (a_mode == SourceResolveMode::kEnsureCustomUniqueID && !extraUniqueID) {
            source.source = {};
            return source;
        }

        source.source = Core::ItemSource {
            .kind = Core::ItemSourceKind::kCustomEnchantment,
            .sourceFormID = a_ring.GetFormID(),
            .customEnchantment = *customSelection.signature,
            .extraUniqueID = extraUniqueID,
        };
        source.sourceExtraList = customSelection.extraList;
        source.vanillaRingSlotEquipped = HasRightWornFlag(customSelection.extraList);
        return source;
    }

    std::optional<EntryRingSource> ResolveMenuEntryRingSource(
        RE::Actor& a_actor,
        RE::InventoryEntryData& a_entry,
        RE::TESObjectARMO& a_ring,
        const SourceResolveMode a_mode
    ) {
        auto* actorEntry = FindEntry(a_actor, a_ring);
        auto menuExtraLists = ResolveMenuEntryExtraLists(a_entry, actorEntry);
        auto source = MakeFormOnlyEntryRingSource(a_ring, menuExtraLists.formOnlyRightWorn);

        source.customFailure = menuExtraLists.customSelection.failure;
        if (source.customFailure != EntryCustomFailure::kNone) {
            source.source = {};
            return source;
        }

        if (!menuExtraLists.customSelection.HasCustomEnchantment()) {
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
        const auto extraUniqueID = a_mode == SourceResolveMode::kEnsureCustomUniqueID
                                       ? EnsureEntryCustomSelectionUniqueID(a_actor, a_ring, customSelection)
                                       : customSelection.uniqueID;
        if (a_mode == SourceResolveMode::kEnsureCustomUniqueID && !extraUniqueID) {
            source.source = {};
            return source;
        }

        source.source = Core::ItemSource {
            .kind = Core::ItemSourceKind::kCustomEnchantment,
            .sourceFormID = a_ring.GetFormID(),
            .customEnchantment = *customSelection.signature,
            .extraUniqueID = extraUniqueID,
        };
        source.sourceExtraList = customSelection.extraList;
        source.vanillaRingSlotEquipped = HasRightWornFlag(customSelection.extraList);
        return source;
    }
}

std::optional<EntryRingSource> ResolveEntryRingSource(
    RE::Actor& a_actor,
    RE::InventoryEntryData& a_entry,
    const SourceResolveMode a_mode,
    const EntryResolveScope a_scope
) {
    auto* ring = AsRing(a_entry.GetObject());
    if (!ring || GetCount(a_actor, *ring) <= 0) {
        return std::nullopt;
    }

    if (a_scope == EntryResolveScope::kMenuRow) {
        return ResolveMenuEntryRingSource(a_actor, a_entry, *ring, a_mode);
    }

    return ResolveActorInventoryEntryRingSource(a_actor, a_entry, *ring, a_mode);
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
        return true;
    }

    return IsClothingRingWithRingEvidence(*a_armor);
}

bool HasClothingRingKeyword(const RE::TESObjectARMO* a_armor) {
    return a_armor && a_armor->HasKeywordString(kClothingRingKeyword);
}

RingInventoryState GetRingInventoryState(RE::Actor& a_actor, const RE::TESObjectARMO& a_ring) {
    auto* entry = Inventory::FindEntry(a_actor, a_ring);
    auto* rightWornExtraList = FindRightWornExtraList(entry);
    const auto rightWorn = entry && entry->IsWorn(false);
    return RingInventoryState {
        .rightWornExtraList = rightWornExtraList,
        .rightWorn = rightWorn,
    };
}
}
