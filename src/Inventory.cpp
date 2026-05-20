#include "Inventory.h"

#include <algorithm>
#include <utility>

namespace Inventory {
namespace {
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

bool MatchesCustomEnchantmentKey(const RE::ExtraDataList* a_extraList, const CustomEnchantmentKey& a_key) {
    const auto key = ReadCustomEnchantmentKey(a_extraList);
    return key && *key == a_key;
}

bool IsRightWorn(const RE::ExtraDataList* a_extraList) {
    return a_extraList && a_extraList->HasType<RE::ExtraWorn>();
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

CustomMatchState FindCustomMatches(RE::InventoryEntryData* a_entry, const CustomEnchantmentKey& a_key) {
    CustomMatchState state;
    if (!a_entry || !a_entry->extraLists) {
        return state;
    }

    for (auto* extraList : *a_entry->extraLists) {
        const auto key = ReadCustomEnchantmentKey(extraList);
        const auto matches = key && *key == a_key;

        if (!matches) {
            continue;
        }

        if (!state.firstExtraList) {
            state.firstExtraList = extraList;
        }

        state.count += std::max(extraList->GetCount(), 1);
        if (IsRightWorn(extraList)) {
            state.rightWornExtraList = extraList;
            state.firstExtraList = extraList;
        }
    }

    return state;
}

CustomMatchState FindSourceMatches(
    RE::Actor& a_actor,
    const RE::TESObjectARMO& a_ring,
    const CustomEnchantmentKey& a_key
) {
    return FindCustomMatches(FindEntry(a_actor, a_ring), a_key);
}

EntryCustomSelection ResolveCustomSelection(RE::InventoryEntryData& a_entry) {
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
            return selection;
        }

        selection.key = key;
        if (!selection.extraList || IsRightWorn(extraList)) {
            selection.extraList = extraList;
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
           && a_armor->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kRing)
           && !a_armor->armorAddons.empty();
}

bool SourceRingState::CanWearSameFormInBothHands() const {
    return count >= 2;
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

    if (!state.rightWornExtraList) {
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
