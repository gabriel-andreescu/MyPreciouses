#include "Inventory.h"

#include <RE/E/ExtraCannotWear.h>

#include <algorithm>
#include <utility>

namespace Inventory {
namespace {
    constexpr auto kClothingRingKeyword = "ClothingRing"sv;

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
            if (Inventory::IsRightWorn(extraList)
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

    [[nodiscard]] std::int32_t CountCustomCopies(const RE::InventoryEntryData* a_entry) {
        if (!a_entry || !a_entry->extraLists) {
            return 0;
        }

        auto count = std::int32_t {0};
        for (auto* extraList : *a_entry->extraLists) {
            if (Inventory::HasCustomEnchantment(extraList)) {
                count += std::max(extraList->GetCount(), 1);
            }
        }
        return count;
    }

    [[nodiscard]] RightWornRing MakeRightWornRing(RE::TESObjectARMO& a_ring, RE::ExtraDataList* a_extraList) {
        return RightWornRing {
            .ring = std::addressof(a_ring),
            .extraList = a_extraList,
            .protectedStack = IsProtectedRingStack(a_extraList),
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
            if (!IsRightWorn(extraList)) {
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

bool IsProtectedRingStack(RE::ExtraDataList* a_extraList) {
    if (!a_extraList) {
        return false;
    }

    if (a_extraList->HasQuestObjectAlias()) {
        return true;
    }

    return a_extraList->HasType<RE::ExtraCannotWear>();
}

bool IsRightWorn(const RE::ExtraDataList* a_extraList) {
    return a_extraList && a_extraList->HasType<RE::ExtraWorn>();
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

bool HasRightWornRing(RE::Actor& a_actor) {
    return FindRightWornRing(a_actor).has_value();
}

bool HasProtectedRightWornRing(RE::Actor& a_actor) {
    const auto rightWorn = FindRightWornRing(a_actor);
    return rightWorn && rightWorn->protectedStack;
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
            if (IsRightWorn(extraList)) {
                state.rightWornExtraList = extraList;
                state.firstExtraList = extraList;
                state.rightWornProtected = IsProtectedRingStack(extraList);
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
    const auto formOnlyCount = std::max(totalCount - customCount, 0);
    auto* rightWornExtraList = FindRightWornFormOnlyExtraList(entry);

    FormOnlySourceMatch state {
        .rightWornExtraList = rightWornExtraList,
        .count = formOnlyCount,
        .rightWornProtected = IsProtectedRingStack(rightWornExtraList),
        .rightWorn = rightWornExtraList != nullptr || (entry && !entry->extraLists && entry->IsWorn(false)),
    };

    return state;
}

namespace {
    EntryCustomSelection ResolveEntryCustomSelection(RE::InventoryEntryData& a_entry) {
        EntryCustomSelection selection;
        if (!a_entry.extraLists) {
            return selection;
        }

        for (auto* extraList : *a_entry.extraLists) {
            const auto signature = ReadCustomEnchantmentSignature(extraList);

            if (!signature) {
                continue;
            }

            if (selection.signature && *selection.signature != *signature) {
                selection.failure = EntryCustomFailure::kMultipleCustomEnchantments;
                selection.extraList = nullptr;
                selection.signature = std::nullopt;
                selection.uniqueID = std::nullopt;
                return selection;
            }

            selection.signature = signature;
            if (!selection.extraList || IsRightWorn(extraList)) {
                selection.extraList = extraList;
                selection.uniqueID = ReadExtraUniqueIDKey(extraList);
            }
        }

        return selection;
    }
}

std::optional<EntryRingSource> ResolveEntryRingSource(RE::Actor& a_actor, RE::InventoryEntryData& a_entry) {
    auto* ring = AsRing(a_entry.GetObject());
    if (!ring || GetCount(a_actor, *ring) <= 0) {
        return std::nullopt;
    }

    EntryRingSource source {
        .ring = ring,
        .source = Core::ItemSource {
            .kind = Core::ItemSourceKind::kFormOnly,
            .sourceFormID = ring->GetFormID(),
        },
        .vanillaRingSlotEquipped = IsFormOnlyRightWorn(a_entry),
    };

    auto customSelection = ResolveEntryCustomSelection(a_entry);
    source.customFailure = customSelection.failure;
    if (source.customFailure != EntryCustomFailure::kNone) {
        source.source = {};
        return source;
    }

    if (!customSelection.HasCustomEnchantment()) {
        return source;
    }

    const auto extraUniqueID = EnsureEntryCustomSelectionUniqueID(a_actor, *ring, customSelection);
    if (!extraUniqueID) {
        source.source = {};
        return source;
    }

    source.source = Core::ItemSource {
        .kind = Core::ItemSourceKind::kCustomEnchantment,
        .sourceFormID = ring->GetFormID(),
        .customEnchantment = *customSelection.signature,
        .extraUniqueID = extraUniqueID,
    };
    source.sourceExtraList = customSelection.extraList;
    source.vanillaRingSlotEquipped = IsRightWorn(customSelection.extraList);
    return source;
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
    return a_armor
           && (a_armor->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kRing)
               || a_armor->HasKeywordString(kClothingRingKeyword))
           && !a_armor->armorAddons.empty();
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
