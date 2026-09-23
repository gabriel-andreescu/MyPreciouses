#include "Equipment/AssignmentStore.h"

#include <RE/Skyrim.h> // IWYU pragma: keep
#include <SKSE/SKSE.h> // IWYU pragma: keep

#include "Core/ActorKey.h"
#include "Core/Assignment.h"
#include "Core/ItemSource.h"
#include "Core/Target.h"
#include "Core/TargetMask.h"
#include "Equipment/SpecialRingRules.h"
#include "Inventory.h"
#include "SourceModelFootprints.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Equipment::AssignmentStore {
namespace {
    std::mutex g_lock;

    [[nodiscard]] std::unordered_map<Core::ActorKey, Core::TargetAssignments>& Snapshots() {
        static auto* snapshots = new std::unordered_map<Core::ActorKey, Core::TargetAssignments>();
        return *snapshots;
    }

    [[nodiscard]] RE::TESObjectARMO* LookupAssignedRing(const RE::FormID a_sourceFormID) {
        return Inventory::AsRing(RE::TESForm::LookupByID(a_sourceFormID));
    }

    [[nodiscard]] bool CanUseActor(const Core::ActorKey a_actor, const std::string_view a_action) {
        if (a_actor) {
            return true;
        }

        SKSE::log::warn("Equipment: actor rejected | action={} | reason=noActor", a_action);
        return false;
    }

    [[nodiscard]] bool CanUseVirtualTarget(const Core::Target a_target, const std::string_view a_action) {
        if (Core::IsVirtualTarget(a_target)) {
            return true;
        }

        SKSE::log::warn(
            "Equipment: virtual target rejected | action={} | target={}",
            a_action,
            Core::TargetName(a_target)
        );
        return false;
    }

    [[nodiscard]] Core::TargetAssignments& GetOrCreateSnapshot(const Core::ActorKey a_actor) {
        return Snapshots()[a_actor];
    }

    // Callers hold g_lock while using the returned snapshot.
    [[nodiscard]] const Core::TargetAssignments* FindSnapshot(const Core::ActorKey a_actor) {
        const auto& snapshots = Snapshots();
        const auto snapshot = snapshots.find(a_actor);
        return snapshot != snapshots.end() ? std::addressof(snapshot->second) : nullptr;
    }

    [[nodiscard]] Core::TargetMask GetProjectedTargets(
        const Core::Assignment& a_assignment,
        const Core::Target a_target
    ) {
        if (const auto* ring = LookupAssignedRing(a_assignment.source.sourceFormID)) {
            return SourceModelFootprints::GetProjectedRingGeometryTargets(*ring, a_target);
        }

        Core::TargetMask mask;
        mask.Add(a_target);
        return mask;
    }

    [[nodiscard]] std::vector<Core::Target> FindConflictingTargets(
        const Core::TargetAssignments& a_snapshot,
        const Core::Target a_target,
        const Core::TargetMask& a_occupiedTargets
    ) {
        std::vector<Core::Target> conflicts;
        for (const auto target : Core::kVirtualTargets) {
            if (target == a_target) {
                continue;
            }

            const auto& assignment = a_snapshot.byTarget[Core::ToIndex(target)];
            if (!assignment.IsAssigned()) {
                continue;
            }

            if (a_occupiedTargets.Intersects(GetProjectedTargets(assignment, target))) {
                conflicts.push_back(target);
            }
        }

        return conflicts;
    }

    [[nodiscard]] bool CanOccupyTargets(
        const Core::TargetMask& a_occupiedTargets,
        const Core::Target a_target,
        const RE::TESObjectARMO& a_ring,
        const std::string_view a_action
    ) {
        if (!a_occupiedTargets.Empty()) {
            return true;
        }

        SKSE::log::warn(
            "Equipment: virtual target rejected | action={} | target={} | source={:08X} | reason=invalidFootprintAnchor",
            a_action,
            Core::TargetName(a_target),
            a_ring.GetFormID()
        );
        return false;
    }

    [[nodiscard]] bool CanUseEnabledTargets(
        const Core::ActorKey a_actor,
        const Core::TargetMask& a_occupiedTargets,
        const Core::Target a_target,
        const RE::TESObjectARMO& a_ring,
        const std::string_view a_action
    ) {
        if (SpecialRingRules::AreTargetsEnabledForSource(a_actor, a_ring, a_occupiedTargets)) {
            return true;
        }

        SKSE::log::warn(
            "Equipment: virtual target rejected | action={} | target={} | source={:08X} | reason=disabledSlot",
            a_action,
            Core::TargetName(a_target),
            a_ring.GetFormID()
        );
        return false;
    }

    void ClearConflictingAssignments(
        Core::TargetAssignments& a_snapshot,
        const std::vector<Core::Target>& a_conflicts
    ) {
        for (const auto target : a_conflicts) {
            a_snapshot.byTarget[Core::ToIndex(target)] = {};
        }
    }

    [[nodiscard]] bool HasAnyAssignment(const Core::TargetAssignments& a_snapshot) {
        return std::ranges::any_of(a_snapshot.byTarget, [](const auto& a_assignment) {
            return a_assignment.IsAssigned();
        });
    }

    [[nodiscard]] std::optional<RE::FormID> ClearMovedSourceAssignment(
        Core::TargetAssignments& a_snapshot,
        const Core::Target a_target,
        const Core::Target a_moveSourceTarget,
        const Core::ItemSource& a_expectedSource
    ) {
        if (!Core::IsVirtualTarget(a_moveSourceTarget) || a_moveSourceTarget == a_target) {
            return std::nullopt;
        }

        auto& assignment = a_snapshot.byTarget[Core::ToIndex(a_moveSourceTarget)];
        if (!assignment.source.IsSameCopy(a_expectedSource)) {
            return std::nullopt;
        }

        const auto retainedEffectSourceFormID = assignment.retainedEffectSourceFormID;
        assignment = {};
        return retainedEffectSourceFormID;
    }

    [[nodiscard]] bool MatchesAssignmentIdentity(
        const Core::Assignment& a_assignment,
        const Core::Assignment& a_expected
    ) {
        return a_assignment.source.IsAssigned() && a_assignment.source == a_expected.source;
    }

}

bool Assign(
    const Core::ActorKey a_actor,
    RE::TESObjectARMO const& a_ring,
    const Core::ItemSource& a_source,
    const Core::Target a_target,
    const std::optional<Core::Target> a_moveSourceTarget
) {
    if (!CanUseActor(a_actor, std::string_view {"assign"})
        || !CanUseVirtualTarget(a_target, std::string_view {"assign"})) {
        return false;
    }

    const auto occupiedTargets = SourceModelFootprints::GetProjectedRingGeometryTargets(a_ring, a_target);
    if (!CanOccupyTargets(occupiedTargets, a_target, a_ring, std::string_view {"assign"})
        || !CanUseEnabledTargets(a_actor, occupiedTargets, a_target, a_ring, std::string_view {"assign"})) {
        return false;
    }

    auto* actor = Core::ResolveActor(a_actor);
    if (!actor) {
        return false;
    }
    const auto current = GetSnapshot(a_actor);
    const auto conflicts = FindConflictingTargets(current, a_target, occupiedTargets);
    std::vector<Core::ItemSource> claimed;
    for (const auto target : Core::kVirtualTargets) {
        if (target
            != a_target
            && target
            != a_moveSourceTarget
            && std::ranges::find(conflicts, target)
            == conflicts.end()) {
            claimed.push_back(current.byTarget[Core::ToIndex(target)].source);
        }
    }
    auto requested = a_source;
    if (a_moveSourceTarget) {
        const auto& moved = current.byTarget[Core::ToIndex(*a_moveSourceTarget)].source;
        if (!requested.Matches(moved)) {
            return false;
        }
        requested = moved;
    }
    const auto copy = Inventory::AcquireCopy(*actor, requested, claimed);
    if (!copy) {
        return false;
    }

    std::scoped_lock const lock(g_lock);
    auto& snapshot = GetOrCreateSnapshot(a_actor);
    auto& assignment = snapshot.byTarget[Core::ToIndex(a_target)];
    auto nextAssignment = Core::Assignment {
        .source = *copy,
    };
    if (assignment.source.sourceFormID == a_ring.GetFormID()) {
        nextAssignment.retainedEffectSourceFormID = assignment.retainedEffectSourceFormID;
    }
    if (a_moveSourceTarget) {
        const auto movedEffectSourceFormID = ClearMovedSourceAssignment(snapshot, a_target, *a_moveSourceTarget, *copy);
        if (!movedEffectSourceFormID) {
            if (!HasAnyAssignment(snapshot)) {
                Snapshots().erase(a_actor);
            }
            return false;
        }

        nextAssignment.retainedEffectSourceFormID = *movedEffectSourceFormID;
    }
    ClearConflictingAssignments(snapshot, conflicts);
    assignment = std::move(nextAssignment);
    Inventory::InvalidateSelections(a_actor.referenceFormID);
    return true;
}

void RemapUniqueID(const Core::ExtraUniqueIDKey& a_previous, const Core::ExtraUniqueIDKey& a_next) {
    std::scoped_lock const lock(g_lock);
    for (auto& [actor, assignments] : Snapshots()) {
        if (assignments.RemapUniqueID(a_previous, a_next)) {
            Inventory::InvalidateSelections(actor.referenceFormID);
        }
    }
}

void Clear(const Core::ActorKey a_actor, const Core::Target a_target) {
    if (!a_actor) {
        return;
    }

    std::scoped_lock const lock(g_lock);
    auto& snapshots = Snapshots();
    if (auto const snapshot = snapshots.find(a_actor); snapshot != snapshots.end()) {
        Inventory::InvalidateSelections(a_actor.referenceFormID);
        snapshot->second.byTarget[Core::ToIndex(a_target)] = {};
        if (!HasAnyAssignment(snapshot->second)) {
            snapshots.erase(snapshot);
        }
    }
}

bool TrySetRetainedEffectSourceFormID(
    const Core::ActorKey a_actor,
    const Core::Target a_target,
    const Core::Assignment& a_expectedAssignment,
    const RE::FormID a_effectSourceFormID
) {
    if (!CanUseActor(a_actor, std::string_view {"setRestoredEffectSource"})
        || !CanUseVirtualTarget(a_target, std::string_view {"setRestoredEffectSource"})
        || a_effectSourceFormID
        == 0) {
        return false;
    }

    std::scoped_lock const lock(g_lock);
    auto& snapshots = Snapshots();
    auto const actorIt = snapshots.find(a_actor);
    if (actorIt == snapshots.end()) {
        return false;
    }

    auto& snapshot = actorIt->second;
    auto& assignment = snapshot.byTarget[Core::ToIndex(a_target)];
    if (!MatchesAssignmentIdentity(assignment, a_expectedAssignment)) {
        if (!HasAnyAssignment(snapshot)) {
            snapshots.erase(actorIt);
        }
        return false;
    }

    assignment.retainedEffectSourceFormID = a_effectSourceFormID;
    return true;
}

Core::Assignment Get(const Core::ActorKey a_actor, const Core::Target a_target) {
    if (!a_actor || !Core::IsVirtualTarget(a_target)) {
        return {};
    }

    std::scoped_lock const lock(g_lock);
    const auto* snapshot = FindSnapshot(a_actor);
    return snapshot ? snapshot->byTarget[Core::ToIndex(a_target)] : Core::Assignment {};
}

Core::TargetAssignments GetSnapshot(const Core::ActorKey a_actor) {
    std::scoped_lock const lock(g_lock);
    const auto* snapshot = a_actor ? FindSnapshot(a_actor) : nullptr;
    return snapshot ? *snapshot : Core::TargetAssignments {};
}

std::vector<Core::ActorAssignments> GetAllSnapshots() {
    std::vector<Core::ActorAssignments> snapshots;
    std::scoped_lock const lock(g_lock);
    const auto& storedSnapshots = Snapshots();
    snapshots.reserve(storedSnapshots.size());
    for (const auto& [actor, assignments] : storedSnapshots) {
        if (!HasAnyAssignment(assignments)) {
            continue;
        }

        snapshots.push_back(
            Core::ActorAssignments {
                .actor = actor,
                .assignments = assignments,
            }
        );
    }
    return snapshots;
}

Core::TargetMask GetMatchingTargets(const Core::ActorKey a_actor, const Core::ItemSource& a_source) {
    std::scoped_lock const lock(g_lock);
    const auto* snapshot = a_actor ? FindSnapshot(a_actor) : nullptr;
    Core::TargetMask targets;
    if (!snapshot) {
        return targets;
    }
    for (const auto target : Core::kVirtualTargets) {
        const auto& assignment = snapshot->byTarget[Core::ToIndex(target)];
        if (a_source.Matches(assignment.source)) {
            targets.Add(target);
        }
    }
    return targets;
}

std::uint32_t CountMatching(
    const Core::ActorKey a_actor,
    const Core::ItemSource& a_source,
    const std::optional<Core::Target> a_excludedTarget
) {
    const auto targets = GetMatchingTargets(a_actor, a_source);
    const auto excludeMatch = a_excludedTarget && targets.Contains(*a_excludedTarget);
    return static_cast<std::uint32_t>(targets.Count() - static_cast<int>(excludeMatch));
}

bool ContainsSource(const Core::ActorKey a_actor, const RE::FormID a_sourceFormID) {
    std::scoped_lock const lock(g_lock);
    const auto* snapshot = a_actor ? FindSnapshot(a_actor) : nullptr;
    return snapshot != nullptr && std::ranges::any_of(snapshot->byTarget, [a_sourceFormID](const auto& a_assignment) {
        return a_assignment.source.MatchesSourceFormID(a_sourceFormID);
    });
}

void ReplaceAll(std::vector<Core::ActorAssignments> a_snapshots) {
    std::unordered_map<Core::ActorKey, Core::TargetAssignments> nextSnapshots;
    nextSnapshots.reserve(a_snapshots.size());
    for (auto& actorSnapshot : a_snapshots) {
        if (!actorSnapshot.actor || !HasAnyAssignment(actorSnapshot.assignments)) {
            continue;
        }

        nextSnapshots.insert_or_assign(actorSnapshot.actor, std::move(actorSnapshot.assignments));
    }

    std::scoped_lock const lock(g_lock);
    Snapshots() = std::move(nextSnapshots);
}

void Revert() {
    std::scoped_lock const lock(g_lock);
    Snapshots().clear();
}
}
