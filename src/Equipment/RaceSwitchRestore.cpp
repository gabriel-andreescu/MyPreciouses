#include "Equipment/RaceSwitchRestore.h"

#include "Equipment/AssignmentStore.h"

#include <algorithm>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Equipment::RaceSwitchRestore {
namespace {
    struct PendingSnapshot {
        RE::FormID raceFormID {0};
        Core::TargetAssignments assignments;
    };

    struct ActiveSwitch {
        RE::FormID targetRaceFormID {0};
        Core::TargetAssignments assignments;
    };

    using ActiveSwitchMap = std::unordered_map<Core::ActorKey, ActiveSwitch>;
    using PendingRestoreMap = std::unordered_map<Core::ActorKey, PendingSnapshot>;

    std::mutex g_lock;

    [[nodiscard]] ActiveSwitchMap& ActiveSwitches() {
        static auto* switches = new ActiveSwitchMap();
        return *switches;
    }

    [[nodiscard]] PendingRestoreMap& PendingRestores() {
        static auto* restores = new PendingRestoreMap();
        return *restores;
    }

    [[nodiscard]] bool HasAnyAssignment(const Core::TargetAssignments& a_snapshot) {
        return std::ranges::any_of(a_snapshot.byTarget, [](const Core::Assignment& a_assignment) {
            return a_assignment.IsAssigned();
        });
    }

    [[nodiscard]] RE::FormID GetRaceFormID(RE::Actor& a_actor) {
        const auto* race = a_actor.GetRace();
        return race ? race->GetFormID() : RE::FormID {0};
    }

}

void BeginRaceSwitch(RE::Actor& a_actor, RE::TESRace& a_targetRace) {
    const auto actorKey = Core::MakeActorKey(a_actor);
    const auto raceFormID = GetRaceFormID(a_actor);
    const auto targetRaceFormID = a_targetRace.GetFormID();
    if (!actorKey || raceFormID == 0 || targetRaceFormID == 0 || raceFormID == targetRaceFormID) {
        return;
    }

    {
        std::scoped_lock lock(g_lock);
        auto& activeSwitches = ActiveSwitches();
        const auto hasPendingRestore = PendingRestores().contains(actorKey);
        const auto activeIt = activeSwitches.find(actorKey);
        if (activeIt != activeSwitches.end()) {
            const auto previousTargetRaceFormID = activeIt->second.targetRaceFormID;
            activeSwitches.erase(activeIt);
            if (previousTargetRaceFormID == raceFormID && !hasPendingRestore) {
                return;
            }
        }

        if (hasPendingRestore) {
            return;
        }
    }

    auto snapshot = AssignmentStore::GetSnapshot(actorKey);
    if (!HasAnyAssignment(snapshot)) {
        return;
    }

    std::scoped_lock lock(g_lock);
    auto& activeSwitches = ActiveSwitches();
    activeSwitches.insert_or_assign(
        actorKey,
        ActiveSwitch {
            .targetRaceFormID = targetRaceFormID,
            .assignments = std::move(snapshot),
        }
    );
}

bool MarkClearedDuringRaceSwitch(RE::Actor& a_actor) {
    const auto actorKey = Core::MakeActorKey(a_actor);
    const auto raceFormID = GetRaceFormID(a_actor);
    if (!actorKey) {
        return false;
    }

    std::scoped_lock lock(g_lock);
    if (PendingRestores().contains(actorKey)) {
        return true;
    }

    auto& activeSwitches = ActiveSwitches();
    auto activeIt = activeSwitches.find(actorKey);
    if (activeIt == activeSwitches.end()) {
        return false;
    }

    if (activeIt->second.targetRaceFormID == 0 || activeIt->second.targetRaceFormID != raceFormID) {
        activeSwitches.erase(activeIt);
        return false;
    }

    auto activeSwitch = std::move(activeIt->second);
    activeSwitches.erase(activeIt);
    if (!HasAnyAssignment(activeSwitch.assignments)) {
        return false;
    }

    PendingRestores().insert_or_assign(
        actorKey,
        PendingSnapshot {
            .raceFormID = activeSwitch.targetRaceFormID,
            .assignments = std::move(activeSwitch.assignments),
        }
    );
    return true;
}

bool HandleRaceSwitchComplete(RE::Actor& a_actor) {
    const auto actorKey = Core::MakeActorKey(a_actor);
    const auto raceFormID = GetRaceFormID(a_actor);
    if (!actorKey || raceFormID == 0) {
        return false;
    }

    std::optional<Core::TargetAssignments> restoredSnapshot;
    {
        std::scoped_lock lock(g_lock);
        auto& activeSwitches = ActiveSwitches();
        auto& pendingRestores = PendingRestores();
        const auto pendingIt = pendingRestores.find(actorKey);
        if (pendingIt != pendingRestores.end()) {
            if (pendingIt->second.raceFormID != raceFormID) {
                restoredSnapshot = std::move(pendingIt->second.assignments);
                pendingRestores.erase(pendingIt);
                activeSwitches.erase(actorKey);
            } else {
                activeSwitches.erase(actorKey);
            }
        } else {
            const auto activeIt = activeSwitches.find(actorKey);
            if (activeIt != activeSwitches.end() && activeIt->second.targetRaceFormID != raceFormID) {
                activeSwitches.erase(activeIt);
            }
        }
    }

    if (restoredSnapshot) {
        AssignmentStore::ReplaceSnapshot(actorKey, std::move(*restoredSnapshot));
        return true;
    }

    return false;
}

std::vector<PendingRestore> GetPendingRestores() {
    std::vector<PendingRestore> restores;
    std::scoped_lock lock(g_lock);
    const auto& pendingRestores = PendingRestores();
    restores.reserve(pendingRestores.size());
    for (const auto& [actor, pending] : pendingRestores) {
        if (!actor || pending.raceFormID == 0 || !HasAnyAssignment(pending.assignments)) {
            continue;
        }

        restores.push_back(
            PendingRestore {
                .actor = actor,
                .raceFormID = pending.raceFormID,
                .assignments = pending.assignments,
            }
        );
    }
    return restores;
}

void ReplacePendingRestores(std::vector<PendingRestore> a_restores) {
    PendingRestoreMap nextRestores;
    nextRestores.reserve(a_restores.size());
    for (auto& restore : a_restores) {
        if (!restore.actor || restore.raceFormID == 0 || !HasAnyAssignment(restore.assignments)) {
            continue;
        }

        nextRestores.insert_or_assign(
            restore.actor,
            PendingSnapshot {
                .raceFormID = restore.raceFormID,
                .assignments = std::move(restore.assignments),
            }
        );
    }

    std::scoped_lock lock(g_lock);
    ActiveSwitches().clear();
    PendingRestores() = std::move(nextRestores);
}

void ClearActiveSwitches() {
    std::scoped_lock lock(g_lock);
    ActiveSwitches().clear();
}

void Revert() {
    std::scoped_lock lock(g_lock);
    ActiveSwitches().clear();
    PendingRestores().clear();
}
}
