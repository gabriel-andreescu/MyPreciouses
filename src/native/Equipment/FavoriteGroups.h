#pragma once

#include "Core/Assignment.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Equipment::FavoriteGroups {
inline constexpr std::size_t kGroupCount = 8;

struct Member {
    RE::FormID formID {0};
    std::int32_t itemID {0};
    [[nodiscard]] bool operator==(const Member&) const = default;
};

struct Row {
    Member member;
    std::vector<Core::ItemSource> sources;
};

struct Layout {
    Core::TargetAssignments assignments;
    std::array<std::int32_t, Core::kAllTargets.size()> itemIDs {};
};

using Groups = std::array<std::optional<Layout>, kGroupCount>;

void Capture(std::size_t a_group, std::span<const Member> a_members, std::span<const Row> a_rows);
[[nodiscard]] std::optional<Layout> GetLayout(std::size_t a_group, std::span<const Member> a_members);
void Remove(std::size_t a_group, std::int32_t a_itemID);
void RemapUniqueID(const Core::ExtraUniqueIDKey& a_previous, const Core::ExtraUniqueIDKey& a_next);
[[nodiscard]] Groups GetAll();
void ReplaceAll(Groups a_groups);
void Revert();
}
