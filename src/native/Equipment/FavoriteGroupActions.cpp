#include "Equipment/FavoriteGroupActions.h"

#include <RE/Skyrim.h> // IWYU pragma: keep

#include "Core/ActorKey.h"
#include "Core/Target.h"
#include "Equipment/AssignmentActions.h"
#include "Inventory.h"

#include <algorithm>

namespace Equipment::FavoriteGroupActions {
void Use(
    RE::Actor& a_actor,
    const std::size_t a_group,
    const std::span<const FavoriteGroups::Member> a_members,
    const bool a_unequipOtherRings
) {
    auto layout = FavoriteGroups::GetLayout(a_group, a_members);
    if (!layout) {
        if (a_unequipOtherRings) {
            static_cast<void>(ClearVirtualAssignments(a_actor));
        }
        return;
    }
    for (auto& assignment : layout->assignments.byTarget) {
        const auto* ring = Inventory::AsRing(RE::TESForm::LookupByID(assignment.source.sourceFormID));
        const auto* entry = ring ? Inventory::FindEntry(a_actor, *ring) : nullptr;
        if (!entry || !entry->IsFavorited()) {
            assignment = {};
        }
    }
    ApplyVirtualLayout(a_actor, layout->assignments, a_unequipOtherRings);
}

MemberResult EquipMember(
    const RE::Actor& a_actor,
    const std::size_t a_group,
    const std::span<const FavoriteGroups::Member> a_members,
    const FavoriteGroups::Member& a_member
) {
    const auto* ring = Inventory::AsRing(RE::TESForm::LookupByID(a_member.formID));
    const auto layout = FavoriteGroups::GetLayout(a_group, a_members);
    if (!ring || !layout) {
        return {};
    }
    const auto savedOn = [&](const Core::Target a_target) {
        const auto index = Core::ToIndex(a_target);
        return layout->assignments.byTarget[index].source.sourceFormID == a_member.formID
               && layout->itemIDs[index] == a_member.itemID;
    };
    if (!std::ranges::any_of(Core::kAllTargets, savedOn)) {
        return {};
    }
    if (!savedOn(Core::kVanillaRingSlotTarget)) {
        return {.placed = true};
    }
    const auto& source = layout->assignments.byTarget[Core::ToIndex(Core::kVanillaRingSlotTarget)].source;
    static_cast<void>(
        EquipTarget({.actor = Core::MakeActorKey(a_actor), .itemSource = source}, Core::kVanillaRingSlotTarget)
    );
    // EquipObject queues the native equip. Callers must reserve its slot before clearing unused worn slots.
    return {.placed = true, .nativeSlotMask = ring->GetSlotMask().underlying()};
}
}
