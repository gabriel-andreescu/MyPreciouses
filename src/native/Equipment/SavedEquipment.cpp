#include "Equipment/SavedEquipment.h"

#include "Core/ActorKey.h"
#include "Core/Assignment.h"
#include "Equipment/AssignmentStore.h"

#include <algorithm>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Equipment::SavedEquipment {
namespace {
    std::mutex g_lock;

    [[nodiscard]] auto& Snapshots() {
        static std::unordered_map<Core::ActorKey, Core::TargetAssignments> snapshots;
        return snapshots;
    }
}

void Capture(const Core::ActorKey a_actor) {
    auto snapshot = AssignmentStore::GetSnapshot(a_actor);
    std::scoped_lock const lock(g_lock);
    if (std::ranges::any_of(snapshot.byTarget, [](const auto& a_assignment) { return a_assignment.IsAssigned(); })) {
        Snapshots().insert_or_assign(a_actor, std::move(snapshot));
    } else {
        Snapshots().erase(a_actor);
    }
}

std::optional<Core::TargetAssignments> Take(const Core::ActorKey a_actor) {
    std::scoped_lock const lock(g_lock);
    auto const snapshot = Snapshots().extract(a_actor);
    return snapshot.empty() ? std::nullopt : std::optional {std::move(snapshot.mapped())};
}

std::vector<Core::ActorAssignments> GetAll() {
    std::scoped_lock const lock(g_lock);
    std::vector<Core::ActorAssignments> snapshots;
    snapshots.reserve(Snapshots().size());
    for (const auto& [actor, assignments] : Snapshots()) {
        snapshots.push_back({.actor = actor, .assignments = assignments});
    }
    return snapshots;
}

void ReplaceAll(std::vector<Core::ActorAssignments> a_snapshots) {
    std::scoped_lock const lock(g_lock);
    Snapshots().clear();
    for (auto& snapshot : a_snapshots) {
        Snapshots().emplace(snapshot.actor, std::move(snapshot.assignments));
    }
}

void Revert() {
    std::scoped_lock const lock(g_lock);
    Snapshots().clear();
}
}
