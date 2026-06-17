#include "Equipment/AssignmentStore.h"

#include "Equipment/SpecialRingRules.h"
#include "Inventory.h"
#include "SourceModelFootprints.h"

#include <algorithm>
#include <mutex>
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

        logger::warn("Equipment: actor rejected | action={} | reason=noActor", a_action);
        return false;
    }

    [[nodiscard]] bool CanUseVirtualTarget(const Core::Target a_target, const std::string_view a_action) {
        if (Core::IsVirtualTarget(a_target)) {
            return true;
        }

        logger::warn(
            "Equipment: virtual target rejected | action={} | target={}",
            a_action,
            Core::TargetName(a_target)
        );
        return false;
    }

    [[nodiscard]] Core::TargetAssignments& GetOrCreateSnapshot(const Core::ActorKey a_actor) {
        return Snapshots()[a_actor];
    }

    [[nodiscard]] Core::TargetAssignments FindSnapshot(const Core::ActorKey a_actor) {
        std::scoped_lock lock(g_lock);
        const auto& snapshots = Snapshots();
        const auto it = snapshots.find(a_actor);
        return it != snapshots.end() ? it->second : Core::TargetAssignments {};
    }

    [[nodiscard]] Core::TargetMask GetProjectedTargets(
        const Core::Assignment& a_assignment,
        const Core::Target a_target
    ) {
        if (const auto* ring = LookupAssignedRing(a_assignment.source.sourceFormID)) {
            return SourceModelFootprints::GetProjectedTargets(*ring, a_target);
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

        logger::warn(
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

        logger::warn(
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
        if (!assignment.source.Matches(a_expectedSource)) {
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

    [[nodiscard]] bool AssignSource(
        const Core::ActorKey a_actor,
        RE::TESObjectARMO& a_ring,
        const Core::ItemSource& a_nextSource,
        const Core::Target a_target,
        const std::optional<Core::Target> a_moveSourceTarget,
        const std::string_view a_action
    ) {
        if (!CanUseActor(a_actor, a_action) || !CanUseVirtualTarget(a_target, a_action)) {
            return false;
        }

        const auto occupiedTargets = SourceModelFootprints::GetProjectedTargets(a_ring, a_target);
        if (!CanOccupyTargets(occupiedTargets, a_target, a_ring, a_action)
            || !CanUseEnabledTargets(a_actor, occupiedTargets, a_target, a_ring, a_action)) {
            return false;
        }

        const auto conflicts = FindConflictingTargets(GetSnapshot(a_actor), a_target, occupiedTargets);

        std::scoped_lock lock(g_lock);
        auto& snapshot = GetOrCreateSnapshot(a_actor);
        auto& assignment = snapshot.byTarget[Core::ToIndex(a_target)];
        auto nextAssignment = Core::Assignment {
            .source = a_nextSource,
        };
        if (assignment.source.sourceFormID == a_ring.GetFormID()) {
            nextAssignment.retainedEffectSourceFormID = assignment.retainedEffectSourceFormID;
        }
        if (a_moveSourceTarget) {
            const auto movedEffectSourceFormID = ClearMovedSourceAssignment(
                snapshot,
                a_target,
                *a_moveSourceTarget,
                a_nextSource
            );
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
        return true;
    }
}

bool AssignForm(
    const Core::ActorKey a_actor,
    RE::TESObjectARMO& a_ring,
    const Core::Target a_target,
    const std::optional<Core::Target> a_moveSourceTarget
) {
    return AssignSource(
        a_actor,
        a_ring,
        Core::ItemSource {
            .kind = Core::ItemSourceKind::kFormOnly,
            .sourceFormID = a_ring.GetFormID(),
        },
        a_target,
        a_moveSourceTarget,
        "assignForm"sv
    );
}

bool AssignCustom(
    const Core::ActorKey a_actor,
    RE::TESObjectARMO& a_ring,
    Core::CustomEnchantmentSignature a_signature,
    std::optional<Core::ExtraUniqueIDKey> a_uniqueID,
    const Core::Target a_target,
    const std::optional<Core::Target> a_moveSourceTarget
) {
    return AssignSource(
        a_actor,
        a_ring,
        Core::ItemSource {
            .kind = Core::ItemSourceKind::kCustomEnchantment,
            .sourceFormID = a_ring.GetFormID(),
            .customEnchantment = std::move(a_signature),
            .extraUniqueID = a_uniqueID,
        },
        a_target,
        a_moveSourceTarget,
        "assignCustom"sv
    );
}

void Clear(const Core::ActorKey a_actor, const Core::Target a_target) {
    if (!a_actor) {
        return;
    }

    std::scoped_lock lock(g_lock);
    auto& snapshots = Snapshots();
    if (auto it = snapshots.find(a_actor); it != snapshots.end()) {
        it->second.byTarget[Core::ToIndex(a_target)] = {};
        if (!HasAnyAssignment(it->second)) {
            snapshots.erase(it);
        }
    }
}

bool TrySetRetainedEffectSourceFormID(
    const Core::ActorKey a_actor,
    const Core::Target a_target,
    const Core::Assignment& a_expectedAssignment,
    const RE::FormID a_effectSourceFormID
) {
    if (!CanUseActor(a_actor, "setRestoredEffectSource"sv)
        || !CanUseVirtualTarget(a_target, "setRestoredEffectSource"sv)
        || a_effectSourceFormID
        == 0) {
        return false;
    }

    std::scoped_lock lock(g_lock);
    auto& snapshots = Snapshots();
    auto actorIt = snapshots.find(a_actor);
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

    std::scoped_lock lock(g_lock);
    const auto& snapshots = Snapshots();
    const auto actorIt = snapshots.find(a_actor);
    if (actorIt == snapshots.end()) {
        return {};
    }

    return actorIt->second.byTarget[Core::ToIndex(a_target)];
}

Core::TargetAssignments GetSnapshot(const Core::ActorKey a_actor) {
    return a_actor ? FindSnapshot(a_actor) : Core::TargetAssignments {};
}

std::vector<Core::ActorAssignments> GetAllSnapshots() {
    std::vector<Core::ActorAssignments> snapshots;
    std::scoped_lock lock(g_lock);
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

std::uint32_t CountMatching(
    const Core::ActorKey a_actor,
    const Core::ItemSource& a_source,
    const std::optional<Core::Target> a_excludedTarget
) {
    const auto snapshot = GetSnapshot(a_actor);
    auto count = std::uint32_t {0};
    for (const auto target : Core::kVirtualTargets) {
        if (a_excludedTarget && *a_excludedTarget == target) {
            continue;
        }

        const auto& assignment = snapshot.byTarget[Core::ToIndex(target)];
        if (assignment.source.Matches(a_source)) {
            ++count;
        }
    }

    return count;
}

bool ContainsSource(const Core::ActorKey a_actor, const RE::FormID a_sourceFormID) {
    const auto snapshot = GetSnapshot(a_actor);
    return std::ranges::any_of(snapshot.byTarget, [a_sourceFormID](const auto& a_assignment) {
        return a_assignment.source.MatchesSourceFormID(a_sourceFormID);
    });
}

void ReplaceSnapshot(const Core::ActorKey a_actor, Core::TargetAssignments a_snapshot) {
    if (!a_actor) {
        return;
    }

    std::scoped_lock lock(g_lock);
    auto& snapshots = Snapshots();
    if (!HasAnyAssignment(a_snapshot)) {
        snapshots.erase(a_actor);
        return;
    }

    snapshots.insert_or_assign(a_actor, std::move(a_snapshot));
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

    std::scoped_lock lock(g_lock);
    Snapshots() = std::move(nextSnapshots);
}

void Revert() {
    std::scoped_lock lock(g_lock);
    Snapshots().clear();
}
}
