#include "Inventory.h"

#include <RE/E/ExtraCannotWear.h>

#include <algorithm>
#include <utility>

namespace Inventory {
namespace {
    constexpr auto kClothingRingKeyword = "ClothingRing"sv;

    [[nodiscard]] std::optional<std::string> ReadPlayerDisplayName(const RE::ExtraDataList& a_extraList) {
        const auto* displayName = a_extraList.GetByType<RE::ExtraTextDisplayData>();
        if (!displayName || !displayName->IsPlayerSet() || displayName->displayName.c_str()[0] == '\0') {
            return std::nullopt;
        }

        return displayName->displayName.c_str();
    }

    [[nodiscard]] RE::ExtraDataList* FindRightWornExtraList(RE::InventoryEntryData* a_entry) {
        if (!a_entry || !a_entry->extraLists) {
            return nullptr;
        }

        for (auto* extraList : *a_entry->extraLists) {
            if (Inventory::IsRightWorn(extraList)) {
                return extraList;
            }
        }

        return nullptr;
    }

    [[nodiscard]] RE::ExtraDataList* FindRightWornFormOnlyExtraList(RE::InventoryEntryData* a_entry) {
        if (!a_entry || !a_entry->extraLists) {
            return nullptr;
        }

        for (auto* extraList : *a_entry->extraLists) {
            if (Inventory::IsRightWorn(extraList) && !Inventory::HasCustomEnchantment(extraList)) {
                return extraList;
            }
        }

        return nullptr;
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

    struct RightWornRingEntry {
        RE::TESObjectARMO* ring {nullptr};
        RE::ExtraDataList* extraList {nullptr};
        bool protectedStack {false};
    };

    [[nodiscard]] RightWornRingEntry MakeRightWornRingEntry(RE::TESObjectARMO& a_ring, RE::ExtraDataList* a_extraList) {
        return RightWornRingEntry {
            .ring = std::addressof(a_ring),
            .extraList = a_extraList,
            .protectedStack = IsProtectedRingStack(a_extraList),
        };
    }

    [[nodiscard]] std::optional<RightWornRingEntry> FindRightWornRingEntry(RE::Actor& a_actor) {
        auto* inventoryChanges = a_actor.GetInventoryChanges();
        if (!inventoryChanges || !inventoryChanges->entryList) {
            return std::nullopt;
        }

        std::optional<RightWornRingEntry> firstRightWorn;
        for (auto* entry : *inventoryChanges->entryList) {
            auto* ring = entry ? AsRing(entry->object) : nullptr;
            if (!ring) {
                continue;
            }

            if (entry->extraLists) {
                for (auto* extraList : *entry->extraLists) {
                    if (!Inventory::IsRightWorn(extraList)) {
                        continue;
                    }

                    auto rightWorn = MakeRightWornRingEntry(*ring, extraList);
                    if (rightWorn.protectedStack) {
                        return rightWorn;
                    }

                    if (!firstRightWorn) {
                        firstRightWorn = rightWorn;
                    }
                }
                continue;
            }

            if (entry->IsWorn(false) && !firstRightWorn) {
                firstRightWorn = MakeRightWornRingEntry(*ring, nullptr);
            }
        }

        return firstRightWorn;
    }

}

bool EntryCustomSelection::HasCustomEnchantment() const {
    return extraList != nullptr && key.has_value() && failure == EntryCustomFailure::kNone;
}

bool CustomMatchState::HasMatch() const {
    return firstExtraList != nullptr && count > 0;
}

bool CustomMatchState::CanWearSameKeyInBothHands() const {
    return count >= 2;
}

bool FormOnlyMatchState::HasMatch() const {
    return count > 0;
}

bool FormOnlyMatchState::CanWearSameFormInBothHands() const {
    return count >= 2;
}

bool operator==(const CustomEnchantmentKey& a_lhs, const CustomEnchantmentKey& a_rhs) {
    return a_lhs.enchantmentFormID
           == a_rhs.enchantmentFormID
           && a_lhs.charge
           == a_rhs.charge
           && a_lhs.removeOnUnequip
           == a_rhs.removeOnUnequip
           && a_lhs.playerDisplayName
           == a_rhs.playerDisplayName;
}

bool operator!=(const CustomEnchantmentKey& a_lhs, const CustomEnchantmentKey& a_rhs) {
    return !(a_lhs == a_rhs);
}

bool operator==(const ExtraListIdentity& a_lhs, const ExtraListIdentity& a_rhs) {
    return a_lhs.baseID == a_rhs.baseID && a_lhs.uniqueID == a_rhs.uniqueID;
}

bool operator!=(const ExtraListIdentity& a_lhs, const ExtraListIdentity& a_rhs) {
    return !(a_lhs == a_rhs);
}

bool HasCustomEnchantment(const RE::ExtraDataList* a_extraList) {
    const auto* enchantment = a_extraList ? a_extraList->GetByType<RE::ExtraEnchantment>() : nullptr;
    return enchantment && enchantment->enchantment;
}

std::optional<CustomEnchantmentKey> ReadCustomEnchantmentKey(const RE::ExtraDataList* a_extraList) {
    const auto* enchantment = a_extraList ? a_extraList->GetByType<RE::ExtraEnchantment>() : nullptr;
    if (!enchantment || !enchantment->enchantment) {
        return std::nullopt;
    }

    CustomEnchantmentKey key {
        .enchantmentFormID = enchantment->enchantment->GetFormID(),
        .charge = enchantment->charge,
        .removeOnUnequip = enchantment->removeOnUnequip,
    };

    if (auto displayName = ReadPlayerDisplayName(*a_extraList)) {
        key.playerDisplayName = std::move(*displayName);
    }

    return key;
}

std::optional<ExtraListIdentity> ReadExtraListIdentity(const RE::ExtraDataList* a_extraList) {
    const auto* uniqueID = a_extraList ? a_extraList->GetByType<RE::ExtraUniqueID>() : nullptr;
    if (!uniqueID) {
        return std::nullopt;
    }

    const auto identity = ExtraListIdentity {
        .baseID = uniqueID->baseID,
        .uniqueID = uniqueID->uniqueID,
    };
    return identity.IsValid() ? std::make_optional(identity) : std::nullopt;
}

std::optional<ExtraListIdentity> EnsureExtraListIdentity(
    RE::Actor& a_actor,
    const RE::TESBoundObject& a_object,
    RE::ExtraDataList& a_extraList
) {
    if (auto existing = ReadExtraListIdentity(std::addressof(a_extraList))) {
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
    return ReadExtraListIdentity(std::addressof(a_extraList));
}

bool MatchesCustomEnchantmentKey(const RE::ExtraDataList* a_extraList, const CustomEnchantmentKey& a_key) {
    const auto key = ReadCustomEnchantmentKey(a_extraList);
    return key && *key == a_key;
}

bool MatchesExtraListIdentity(const RE::ExtraDataList* a_extraList, const ExtraListIdentity& a_identity) {
    const auto identity = ReadExtraListIdentity(a_extraList);
    return identity && *identity == a_identity;
}

bool MatchesCustomSelection(
    const RE::ExtraDataList* a_extraList,
    const CustomEnchantmentKey& a_key,
    const std::optional<ExtraListIdentity>& a_identity
) {
    if (!MatchesCustomEnchantmentKey(a_extraList, a_key)) {
        return false;
    }

    return !a_identity || MatchesExtraListIdentity(a_extraList, *a_identity);
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

bool HasRightWornRing(RE::Actor& a_actor) {
    return FindRightWornRingEntry(a_actor).has_value();
}

bool HasProtectedRightWornRing(RE::Actor& a_actor) {
    const auto rightWorn = FindRightWornRingEntry(a_actor);
    return rightWorn && rightWorn->protectedStack;
}

RightWornRingUnequipResult UnequipRightWornRing(RE::Actor& a_actor) {
    const auto rightWorn = FindRightWornRingEntry(a_actor);
    if (!rightWorn) {
        return RightWornRingUnequipResult::kNone;
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

    return HasRightWornRing(a_actor) ? RightWornRingUnequipResult::kFailed : RightWornRingUnequipResult::kUnequipped;
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

CustomMatchState FindCustomMatches(
    RE::InventoryEntryData* a_entry,
    const CustomEnchantmentKey& a_key,
    const std::optional<ExtraListIdentity>& a_identity
) {
    CustomMatchState state;
    if (!a_entry || !a_entry->extraLists) {
        return state;
    }

    for (auto* extraList : *a_entry->extraLists) {
        if (!MatchesCustomSelection(extraList, a_key, a_identity)) {
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

CustomMatchState FindSourceMatches(
    RE::Actor& a_actor,
    const RE::TESObjectARMO& a_ring,
    const CustomEnchantmentKey& a_key,
    const std::optional<ExtraListIdentity>& a_identity
) {
    return FindCustomMatches(FindEntry(a_actor, a_ring), a_key, a_identity);
}

FormOnlyMatchState FindFormOnlyMatches(RE::Actor& a_actor, const RE::TESObjectARMO& a_ring) {
    auto* entry = FindEntry(a_actor, a_ring);
    const auto totalCount = GetCount(a_actor, a_ring);
    const auto customCount = CountCustomCopies(entry);
    const auto formOnlyCount = std::max(totalCount - customCount, 0);
    auto* rightWornExtraList = FindRightWornFormOnlyExtraList(entry);

    FormOnlyMatchState state {
        .rightWornExtraList = rightWornExtraList,
        .count = formOnlyCount,
        .rightWornProtected = IsProtectedRingStack(rightWornExtraList),
        .rightWorn = rightWornExtraList != nullptr || (entry && !entry->extraLists && entry->IsWorn(false)),
    };

    return state;
}

EntryCustomSelection ResolveEntryCustomSelection(RE::InventoryEntryData& a_entry) {
    EntryCustomSelection selection;
    if (!a_entry.extraLists) {
        return selection;
    }

    for (auto* extraList : *a_entry.extraLists) {
        const auto key = ReadCustomEnchantmentKey(extraList);

        if (!key) {
            continue;
        }

        if (selection.key && *selection.key != *key) {
            selection.failure = EntryCustomFailure::kMultipleCustomKeys;
            selection.extraList = nullptr;
            selection.key = std::nullopt;
            selection.identity = std::nullopt;
            return selection;
        }

        selection.key = key;
        if (!selection.extraList || IsRightWorn(extraList)) {
            selection.extraList = extraList;
            selection.identity = ReadExtraListIdentity(extraList);
        }
    }

    return selection;
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

bool SourceRingState::HasRightWornEnchantment() const {
    return rightWornEnchanted;
}

SourceRingState GetSourceState(RE::Actor& a_actor, const RE::TESObjectARMO& a_ring) {
    auto* entry = Inventory::FindEntry(a_actor, a_ring);
    auto* rightWornExtraList = FindRightWornExtraList(entry);
    const auto rightWorn = entry && entry->IsWorn(false);
    return SourceRingState {
        .entry = entry,
        .rightWornExtraList = rightWornExtraList,
        .count = Inventory::GetCount(a_actor, a_ring),
        .rightWornProtected = IsProtectedRingStack(rightWornExtraList),
        .rightWorn = rightWorn,
        .rightWornEnchanted = rightWorn
                              && (a_ring.formEnchanting || Inventory::HasCustomEnchantment(rightWornExtraList)),
    };
}

bool UnequipRightWornSource(RE::Actor& a_actor, RE::TESObjectARMO& a_ring) {
    const auto state = GetSourceState(a_actor, a_ring);
    if (!state.rightWorn) {
        return true;
    }

    if (state.rightWornProtected) {
        return false;
    }

    auto* equipManager = RE::ActorEquipManager::GetSingleton();
    if (!equipManager) {
        return false;
    }

    equipManager->UnequipObject(
        std::addressof(a_actor),
        std::addressof(a_ring),
        state.rightWornExtraList,
        1,
        nullptr,
        true,
        false,
        false,
        true,
        nullptr
    );

    const auto afterUnequip = GetSourceState(a_actor, a_ring);
    return !afterUnequip.rightWorn;
}
}
