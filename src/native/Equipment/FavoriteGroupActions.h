#pragma once

#include "Equipment/FavoriteGroups.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Equipment::FavoriteGroupActions {
struct MemberResult {
    bool placed {false};
    std::uint32_t nativeSlotMask {0};
};

void Use(
    RE::Actor& a_actor,
    std::size_t a_group,
    std::span<const FavoriteGroups::Member> a_members,
    bool a_unequipOtherRings
);
[[nodiscard]] MemberResult EquipMember(
    const RE::Actor& a_actor,
    std::size_t a_group,
    std::span<const FavoriteGroups::Member> a_members,
    const FavoriteGroups::Member& a_member
);
}
