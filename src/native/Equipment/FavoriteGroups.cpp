#include "Equipment/FavoriteGroups.h"

#include "Core/ActorKey.h"
#include "Equipment/AssignmentStore.h"
#include "Inventory.h"

#include <algorithm>
#include <mutex>
#include <utility>

namespace Equipment::FavoriteGroups {
namespace {
    std::mutex g_lock;
    Groups g_groups;

    std::optional<std::int32_t> FindMember(
        const Core::ItemSource& a_source,
        const std::span<const Member> a_members,
        const std::span<const Row> a_rows
    ) {
        for (const auto& row : a_rows) {
            if (std::ranges::find(a_members, row.member)
                != a_members.end()
                && std::ranges::any_of(row.sources, [&](const auto& a_rowSource) {
                       return a_rowSource.Matches(a_source);
                   })) {
                return row.member.itemID;
            }
        }
        return std::nullopt;
    }
}

void Capture(const std::size_t a_group, const std::span<const Member> a_members, const std::span<const Row> a_rows) {
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return;
    }
    Layout layout {.assignments = AssignmentStore::GetSnapshot(Core::GetPlayerActorKey())};
    if (const auto native = Inventory::CaptureRightWornRing(*player)) {
        layout.assignments.byTarget[Core::ToIndex(Core::kVanillaRingSlotTarget)].source = *native;
    }
    for (const auto target : Core::kAllTargets) {
        const auto index = Core::ToIndex(target);
        auto& assignment = layout.assignments.byTarget[index];
        const auto member = FindMember(assignment.source, a_members, a_rows);
        if (member) {
            layout.itemIDs[index] = *member;
            assignment.retainedEffectSourceFormID = 0;
        } else {
            assignment = {};
        }
    }
    std::scoped_lock const lock(g_lock);
    g_groups[a_group] = std::move(layout);
}

std::optional<Layout> GetLayout(const std::size_t a_group, const std::span<const Member> a_members) {
    std::scoped_lock const lock(g_lock);
    auto layout = g_groups[a_group];
    if (layout) {
        for (const auto target : Core::kAllTargets) {
            const auto index = Core::ToIndex(target);
            auto& assignment = layout->assignments.byTarget[index];
            const Member member {.formID = assignment.source.sourceFormID, .itemID = layout->itemIDs[index]};
            if (std::ranges::find(a_members, member) == a_members.end()) {
                assignment = {};
                layout->itemIDs[index] = 0;
            }
        }
    }
    return layout;
}

void Remove(const std::size_t a_group, const std::int32_t a_itemID) {
    std::scoped_lock const lock(g_lock);
    if (auto& layout = g_groups[a_group]; layout) {
        for (const auto target : Core::kAllTargets) {
            const auto index = Core::ToIndex(target);
            if (layout->itemIDs[index] == a_itemID) {
                layout->assignments.byTarget[index] = {};
                layout->itemIDs[index] = 0;
            }
        }
    }
}

void RemapUniqueID(const Core::ExtraUniqueIDKey& a_previous, const Core::ExtraUniqueIDKey& a_next) {
    std::scoped_lock const lock(g_lock);
    for (auto& layout : g_groups) {
        if (layout) {
            layout->assignments.RemapUniqueID(a_previous, a_next);
        }
    }
}

Groups GetAll() {
    std::scoped_lock const lock(g_lock);
    return g_groups;
}

void ReplaceAll(Groups a_groups) {
    std::scoped_lock const lock(g_lock);
    g_groups = std::move(a_groups);
}

void Revert() {
    ReplaceAll({});
}
}
