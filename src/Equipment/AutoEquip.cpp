#include "Equipment/AutoEquip.h"

#include "Equipment/AssignmentStore.h"
#include "Inventory.h"
#include "Settings.h"
#include "SourceModelFootprints.h"
#include "UI.h"
#include "VirtualSlots.h"

#include <RE/C/ContainerMenu.h>
#include <RE/M/Misc.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Equipment::AutoEquip {
namespace {
    using ReasonMask = std::uint32_t;

    struct RefreshState {
        bool queued {false};
        bool running {false};
        bool pending {false};
        ReasonMask reasons {0};
    };

    struct Candidate {
        RE::InventoryEntryData* entry {nullptr};
        RE::TESObjectARMO* ring {nullptr};
        Core::ItemSource source;
        RE::ExtraDataList* sourceExtraList {nullptr};
        Core::TargetMask sourceTargets;
        std::int32_t value {0};
        std::uint32_t availableCopies {0};
    };

    struct PlannedAssignment {
        std::size_t candidateIndex {0};
        Core::Target target;
    };

    struct AppliedAssignment {
        RE::TESObjectARMO* ring {nullptr};
        Core::ItemSource source;
        Core::Target target;
    };

    std::mutex g_lock;
    std::optional<Core::ActorKey> g_followerTradeActor;

    [[nodiscard]] std::unordered_set<Core::ActorKey>& RegisteredActors() {
        static auto* actors = new std::unordered_set<Core::ActorKey>();
        return *actors;
    }

    [[nodiscard]] std::unordered_map<Core::ActorKey, RefreshState>& RefreshStates() {
        static auto* states = new std::unordered_map<Core::ActorKey, RefreshState>();
        return *states;
    }

    [[nodiscard]] constexpr ReasonMask ToReasonMask(const RefreshReason a_reason) {
        return static_cast<ReasonMask>(a_reason);
    }

    [[nodiscard]] bool IsPlayerActor(const Core::ActorKey a_actor) {
        const auto player = Core::GetPlayerActorKey();
        return player && a_actor == player;
    }

    [[nodiscard]] bool HasAnyAssignment(const Core::TargetAssignments& a_snapshot) {
        return std::ranges::any_of(a_snapshot.byTarget, [](const Core::Assignment& a_assignment) {
            return a_assignment.IsAssigned();
        });
    }

    [[nodiscard]] bool HasStoredAssignment(const Core::ActorKey a_actor) {
        return HasAnyAssignment(AssignmentStore::GetSnapshot(a_actor));
    }

    [[nodiscard]] bool MatchesTradeActor(const Core::ActorKey a_actor) {
        std::scoped_lock lock(g_lock);
        return g_followerTradeActor && *g_followerTradeActor == a_actor;
    }

    [[nodiscard]] bool IsRegisteredActor(const Core::ActorKey a_actor) {
        std::scoped_lock lock(g_lock);
        return RegisteredActors().contains(a_actor);
    }

    [[nodiscard]] std::optional<Core::ActorKey> ResolveOpenFollowerTradeTarget() {
        if (RE::ContainerMenu::GetContainerMode() != RE::ContainerMenu::ContainerMode::kNPCMode) {
            return std::nullopt;
        }

        const auto targetHandle = RE::ContainerMenu::GetTargetRefHandle();
        RE::NiPointer<RE::TESObjectREFR> targetPtr;
        if (!RE::LookupReferenceByHandle(targetHandle, targetPtr)) {
            return std::nullopt;
        }

        auto* target = targetPtr.get();
        auto* actor = target ? target->As<RE::Actor>() : nullptr;
        if (!actor || actor->IsPlayerRef() || !actor->IsPlayerTeammate()) {
            return std::nullopt;
        }

        return Core::MakeActorKey(*actor);
    }

    void SetFollowerTradeActor(const std::optional<Core::ActorKey> a_actor) {
        std::scoped_lock lock(g_lock);
        g_followerTradeActor = a_actor;
        if (a_actor) {
            RegisteredActors().insert(*a_actor);
        }
    }

    [[nodiscard]] bool EventTouchesActor(const RE::TESContainerChangedEvent& a_event, const Core::ActorKey a_actor) {
        return a_actor
               && (a_event.oldContainer == a_actor.referenceFormID || a_event.newContainer == a_actor.referenceFormID);
    }

    void AddTargets(Core::TargetMask& a_targets, const Core::TargetMask& a_additions) {
        for (const auto target : Core::kAllTargets) {
            if (a_additions.Contains(target)) {
                a_targets.Add(target);
            }
        }
    }

    [[nodiscard]] std::uint32_t AvailableCopyCount(
        RE::Actor& a_actor,
        const RE::TESObjectARMO& a_ring,
        const Core::ItemSource& a_source
    ) {
        if (a_source.IsCustomEnchantment()) {
            const auto matches = Inventory::FindCustomSourceMatches(
                a_actor,
                a_ring,
                a_source.customEnchantment,
                a_source.extraUniqueID
            );
            const auto rightWorn = matches.rightWornExtraList != nullptr;
            if (!a_source.extraUniqueID && rightWorn) {
                return 0;
            }

            const auto reservedRightRing = rightWorn ? 1 : 0;
            const auto available = std::max(matches.count - reservedRightRing, 0);
            return a_source.extraUniqueID ? static_cast<std::uint32_t>(available)
                                          : static_cast<std::uint32_t>(std::min(available, 1));
        }

        if (a_source.IsFormOnly()) {
            const auto matches = Inventory::FindFormOnlySourceMatches(a_actor, a_ring);
            const auto reservedRightRing = matches.rightWorn ? 1 : 0;
            return static_cast<std::uint32_t>(std::max(matches.count - reservedRightRing, 0));
        }

        return 0;
    }

    [[nodiscard]] std::vector<Candidate> CollectCandidates(RE::Actor& a_actor) {
        std::vector<Candidate> candidates;
        auto* inventoryChanges = a_actor.GetInventoryChanges();
        if (!inventoryChanges || !inventoryChanges->entryList) {
            return candidates;
        }

        for (auto* entry : *inventoryChanges->entryList) {
            if (!entry) {
                continue;
            }

            auto source = Inventory::ResolveEntryRingSource(a_actor, *entry, Inventory::SourceResolveMode::kReadOnly);
            if (!source || !source->ring || !source->source.IsAssigned()) {
                continue;
            }

            const auto sourceTargets = SourceModelFootprints::GetSourceTargets(*source->ring);
            if (sourceTargets.Empty()) {
                continue;
            }

            const auto availableCopies = AvailableCopyCount(a_actor, *source->ring, source->source);
            if (availableCopies == 0) {
                continue;
            }

            candidates.push_back(
                Candidate {
                    .entry = entry,
                    .ring = source->ring,
                    .source = source->source,
                    .sourceExtraList = source->sourceExtraList,
                    .sourceTargets = sourceTargets,
                    .value = entry->GetValue(),
                    .availableCopies = availableCopies,
                }
            );
        }

        return candidates;
    }

    [[nodiscard]] std::uint32_t OptionalUniqueBaseID(const Core::ItemSource& a_source) {
        return a_source.extraUniqueID ? a_source.extraUniqueID->baseID : 0;
    }

    [[nodiscard]] std::uint16_t OptionalUniqueID(const Core::ItemSource& a_source) {
        return a_source.extraUniqueID ? a_source.extraUniqueID->uniqueID : 0;
    }

    [[nodiscard]] bool IsSingleFingerRing(const Candidate& a_candidate) {
        return a_candidate.sourceTargets.Count() == 1;
    }

    [[nodiscard]] bool IsBetterCandidate(const Candidate& a_lhs, const Candidate& a_rhs, const Core::Target a_target) {
        const auto lhsSingle = IsSingleFingerRing(a_lhs);
        const auto rhsSingle = IsSingleFingerRing(a_rhs);
        if (lhsSingle != rhsSingle) {
            return lhsSingle;
        }

        if (a_lhs.value != a_rhs.value) {
            return a_lhs.value > a_rhs.value;
        }

        const auto lhsProjected = SourceModelFootprints::GetProjectedTargets(a_lhs.sourceTargets, a_target).Count();
        const auto rhsProjected = SourceModelFootprints::GetProjectedTargets(a_rhs.sourceTargets, a_target).Count();
        if (lhsProjected != rhsProjected) {
            return lhsProjected < rhsProjected;
        }

        if (a_lhs.sourceTargets.Count() != a_rhs.sourceTargets.Count()) {
            return a_lhs.sourceTargets.Count() < a_rhs.sourceTargets.Count();
        }

        if (a_lhs.source.sourceFormID != a_rhs.source.sourceFormID) {
            return a_lhs.source.sourceFormID < a_rhs.source.sourceFormID;
        }

        if (a_lhs.source.kind != a_rhs.source.kind) {
            return std::to_underlying(a_lhs.source.kind) < std::to_underlying(a_rhs.source.kind);
        }

        if (a_lhs.source.customEnchantment.enchantmentFormID != a_rhs.source.customEnchantment.enchantmentFormID) {
            return a_lhs.source.customEnchantment.enchantmentFormID < a_rhs.source.customEnchantment.enchantmentFormID;
        }

        if (a_lhs.source.customEnchantment.charge != a_rhs.source.customEnchantment.charge) {
            return a_lhs.source.customEnchantment.charge < a_rhs.source.customEnchantment.charge;
        }

        if (a_lhs.source.customEnchantment.removeOnUnequip != a_rhs.source.customEnchantment.removeOnUnequip) {
            return !a_lhs.source.customEnchantment.removeOnUnequip;
        }

        if (OptionalUniqueBaseID(a_lhs.source) != OptionalUniqueBaseID(a_rhs.source)) {
            return OptionalUniqueBaseID(a_lhs.source) < OptionalUniqueBaseID(a_rhs.source);
        }

        if (OptionalUniqueID(a_lhs.source) != OptionalUniqueID(a_rhs.source)) {
            return OptionalUniqueID(a_lhs.source) < OptionalUniqueID(a_rhs.source);
        }

        return a_lhs.source.customEnchantment.playerDisplayName < a_rhs.source.customEnchantment.playerDisplayName;
    }

    [[nodiscard]] bool CanPlaceCandidate(
        const Candidate& a_candidate,
        const Core::Target a_target,
        const Core::TargetMask& a_occupiedTargets
    ) {
        const auto projectedTargets = SourceModelFootprints::GetProjectedTargets(a_candidate.sourceTargets, a_target);
        return !projectedTargets.Empty()
               && Settings::GetSingleton()->AreTargetsEnabled(projectedTargets)
               && !a_occupiedTargets.Intersects(projectedTargets);
    }

    [[nodiscard]] std::optional<std::size_t> SelectCandidateForTarget(
        const std::vector<Candidate>& a_candidates,
        const std::vector<std::uint32_t>& a_usedCopies,
        const Core::Target a_target,
        const Core::TargetMask& a_occupiedTargets
    ) {
        std::optional<std::size_t> bestIndex;
        for (std::size_t index = 0; index < a_candidates.size(); ++index) {
            const auto& candidate = a_candidates[index];
            if (a_usedCopies[index]
                >= candidate.availableCopies
                || !CanPlaceCandidate(candidate, a_target, a_occupiedTargets)) {
                continue;
            }

            if (!bestIndex || IsBetterCandidate(candidate, a_candidates[*bestIndex], a_target)) {
                bestIndex = index;
            }
        }

        return bestIndex;
    }

    [[nodiscard]] Core::TargetMask GetInitialOccupiedTargets(RE::Actor& a_actor) {
        Core::TargetMask occupiedTargets;
        const auto rightWorn = Inventory::FindRightWornRing(a_actor);
        if (rightWorn && rightWorn->ring) {
            AddTargets(
                occupiedTargets,
                SourceModelFootprints::GetProjectedTargets(*rightWorn->ring, Core::kVanillaRingSlotTarget)
            );
        }
        return occupiedTargets;
    }

    [[nodiscard]] std::vector<PlannedAssignment> BuildPlan(
        RE::Actor& a_actor,
        const std::vector<Candidate>& a_candidates
    ) {
        std::vector<PlannedAssignment> plan;
        plan.reserve(Core::kVirtualTargets.size());

        auto occupiedTargets = GetInitialOccupiedTargets(a_actor);
        std::vector<std::uint32_t> usedCopies(a_candidates.size(), 0);

        for (const auto target : Core::kVirtualTargets) {
            const auto candidateIndex = SelectCandidateForTarget(a_candidates, usedCopies, target, occupiedTargets);
            if (!candidateIndex) {
                continue;
            }

            const auto& candidate = a_candidates[*candidateIndex];
            ++usedCopies[*candidateIndex];
            AddTargets(occupiedTargets, SourceModelFootprints::GetProjectedTargets(candidate.sourceTargets, target));
            plan.push_back(
                PlannedAssignment {
                    .candidateIndex = *candidateIndex,
                    .target = target,
                }
            );
        }

        return plan;
    }

    [[nodiscard]] std::optional<Core::ItemSource> ResolveAppliedSource(
        RE::Actor& a_actor,
        const Candidate& a_candidate
    ) {
        if (!a_candidate.source.IsCustomEnchantment() || a_candidate.source.extraUniqueID) {
            return a_candidate.source;
        }

        if (!a_candidate.entry) {
            return std::nullopt;
        }

        auto source = Inventory::ResolveEntryRingSource(
            a_actor,
            *a_candidate.entry,
            Inventory::SourceResolveMode::kEnsureCustomUniqueID
        );
        if (!source || !source->source.IsCustomEnchantment()) {
            logger::warn(
                "AutoEquip: custom ring skipped | actor={:08X} | source={:08X} | reason=uniqueIDUnavailable",
                a_actor.GetFormID(),
                a_candidate.source.sourceFormID
            );
            return std::nullopt;
        }

        return source->source;
    }

    [[nodiscard]] std::array<std::optional<AppliedAssignment>, Core::kAllTargets.size()> ResolveAppliedPlan(
        RE::Actor& a_actor,
        const std::vector<Candidate>& a_candidates,
        const std::vector<PlannedAssignment>& a_plan
    ) {
        std::array<std::optional<AppliedAssignment>, Core::kAllTargets.size()> appliedPlan;
        for (const auto& planned : a_plan) {
            if (planned.candidateIndex >= a_candidates.size()) {
                continue;
            }

            const auto& candidate = a_candidates[planned.candidateIndex];
            auto source = ResolveAppliedSource(a_actor, candidate);
            if (!source || !source->IsAssigned() || !candidate.ring) {
                continue;
            }

            appliedPlan[Core::ToIndex(planned.target)] = AppliedAssignment {
                .ring = candidate.ring,
                .source = std::move(*source),
                .target = planned.target,
            };
        }

        return appliedPlan;
    }

    [[nodiscard]] bool AssignAppliedSource(const Core::ActorKey a_actor, const AppliedAssignment& a_assignment) {
        if (!a_assignment.ring) {
            return false;
        }

        if (a_assignment.source.IsCustomEnchantment()) {
            return AssignmentStore::AssignCustom(
                a_actor,
                *a_assignment.ring,
                a_assignment.source.customEnchantment,
                a_assignment.source.extraUniqueID,
                a_assignment.target
            );
        }

        if (a_assignment.source.IsFormOnly()) {
            return AssignmentStore::AssignForm(a_actor, *a_assignment.ring, a_assignment.target);
        }

        return false;
    }

    [[nodiscard]] bool ApplyPlan(
        const Core::ActorKey a_actor,
        RE::Actor& a_actorRef,
        const std::vector<Candidate>& a_candidates,
        const std::vector<PlannedAssignment>& a_plan
    ) {
        auto changed = false;
        const auto appliedPlan = ResolveAppliedPlan(a_actorRef, a_candidates, a_plan);
        const auto current = AssignmentStore::GetSnapshot(a_actor);

        for (const auto target : Core::kVirtualTargets) {
            const auto index = Core::ToIndex(target);
            if (!appliedPlan[index] && current.byTarget[index].IsAssigned()) {
                AssignmentStore::Clear(a_actor, target);
                changed = true;
            }
        }

        for (const auto target : Core::kVirtualTargets) {
            const auto index = Core::ToIndex(target);
            const auto& desired = appliedPlan[index];
            if (!desired) {
                continue;
            }

            if (current.byTarget[index].source == desired->source) {
                continue;
            }

            if (AssignAppliedSource(a_actor, *desired)) {
                changed = true;
                continue;
            }

            AssignmentStore::Clear(a_actor, target);
            changed = true;
        }

        return changed;
    }

    [[nodiscard]] bool ShouldRefreshWhenUnchanged(const ReasonMask a_reasons) {
        constexpr auto refreshReasons = ToReasonMask(RefreshReason::kLoad)
                                        | ToReasonMask(RefreshReason::kSettingsChanged)
                                        | ToReasonMask(RefreshReason::kLoad3D);
        return (a_reasons & refreshReasons) != 0;
    }

    void RunRefresh(const Core::ActorKey a_actor, const ReasonMask a_reasons) {
        if (!IsManagedActor(a_actor)) {
            return;
        }

        auto* actor = Core::ResolveActor(a_actor);
        if (!actor || actor->IsPlayerRef()) {
            return;
        }

        const auto candidates = CollectCandidates(*actor);
        const auto plan = BuildPlan(*actor, candidates);
        const auto changed = ApplyPlan(a_actor, *actor, candidates, plan);
        if (changed || ShouldRefreshWhenUnchanged(a_reasons)) {
            VirtualSlots::RequestRefresh(a_actor);
        }
        if (changed) {
            UI::RefreshRingItemRows();
        }

        logger::debug(
            "AutoEquip: refreshed | actor={:08X} | candidates={} | assignments={} | changed={} | reasons={:08X}",
            a_actor.referenceFormID,
            candidates.size(),
            plan.size(),
            changed,
            a_reasons
        );
    }

    [[nodiscard]] ReasonMask TakePendingReasons(const Core::ActorKey a_actor) {
        std::scoped_lock lock(g_lock);
        auto& state = RefreshStates()[a_actor];
        state.queued = false;
        state.running = true;
        state.pending = false;
        const auto reasons = state.reasons;
        state.reasons = 0;
        return reasons;
    }

    [[nodiscard]] bool FinishRefreshIteration(const Core::ActorKey a_actor) {
        std::scoped_lock lock(g_lock);
        auto& states = RefreshStates();
        auto it = states.find(a_actor);
        if (it == states.end()) {
            return false;
        }

        auto& state = it->second;
        if (state.pending) {
            return true;
        }

        states.erase(it);
        return false;
    }

    void RunRefreshLoop(const Core::ActorKey a_actor) {
        for (;;) {
            const auto reasons = TakePendingReasons(a_actor);
            RunRefresh(a_actor, reasons);
            if (!FinishRefreshIteration(a_actor)) {
                return;
            }
        }
    }
}

void HandleContainerMenuOpened() {
    auto target = ResolveOpenFollowerTradeTarget();
    SetFollowerTradeActor(target);
    if (target) {
        logger::debug("AutoEquip: follower trade target registered | actor={:08X}", target->referenceFormID);
    }
}

void HandleContainerMenuClosed() {
    SetFollowerTradeActor(std::nullopt);
}

std::optional<Core::ActorKey> GetFollowerTradeTarget() {
    std::scoped_lock lock(g_lock);
    return g_followerTradeActor;
}

bool IsManagedActor(const Core::ActorKey a_actor) {
    return a_actor
           && !IsPlayerActor(a_actor)
           && (MatchesTradeActor(a_actor) || IsRegisteredActor(a_actor) || HasStoredAssignment(a_actor));
}

void QueueRefresh(const Core::ActorKey a_actor, const RefreshReason a_reason) {
    if (!a_actor || IsPlayerActor(a_actor)) {
        return;
    }

    auto shouldQueue = false;
    {
        std::scoped_lock lock(g_lock);
        auto& state = RefreshStates()[a_actor];
        state.pending = true;
        state.reasons |= ToReasonMask(a_reason);
        if (!state.queued && !state.running) {
            state.queued = true;
            shouldQueue = true;
        }
    }

    if (shouldQueue) {
        stl::add_task([a_actor] {
            RunRefreshLoop(a_actor);
        });
    }
}

void QueueRefreshStoredActors(const RefreshReason a_reason) {
    const auto snapshots = AssignmentStore::GetAllSnapshots();
    for (const auto& snapshot : snapshots) {
        if (IsPlayerActor(snapshot.actor)) {
            VirtualSlots::RequestRefresh(snapshot.actor);
            continue;
        }

        QueueRefresh(snapshot.actor, a_reason);
    }
}

void HandleContainerChanged(const RE::TESContainerChangedEvent& a_event) {
    if (!Inventory::AsRing(RE::TESForm::LookupByID(a_event.baseObj))) {
        return;
    }

    const auto tradeActor = GetFollowerTradeTarget();
    if (tradeActor && EventTouchesActor(a_event, *tradeActor)) {
        QueueRefresh(*tradeActor, RefreshReason::kContainerChanged);
    }

    const auto snapshots = AssignmentStore::GetAllSnapshots();
    for (const auto& snapshot : snapshots) {
        if (IsPlayerActor(snapshot.actor)
            || (tradeActor && snapshot.actor == *tradeActor)
            || !EventTouchesActor(a_event, snapshot.actor)
            || !AssignmentStore::ContainsSource(snapshot.actor, a_event.baseObj)) {
            continue;
        }

        QueueRefresh(snapshot.actor, RefreshReason::kContainerChanged);
    }
}

void HandleEquipEvent(RE::Actor& a_actor, const RE::FormID a_sourceFormID) {
    if (!Inventory::AsRing(RE::TESForm::LookupByID(a_sourceFormID))) {
        return;
    }

    const auto actor = Core::MakeActorKey(a_actor);
    if (IsManagedActor(actor)) {
        QueueRefresh(actor, RefreshReason::kEquipChanged);
    }
}

void HandleActorLoad3D(RE::Actor& a_actor) {
    const auto actor = Core::MakeActorKey(a_actor);
    if (!actor || IsPlayerActor(actor)) {
        return;
    }

    if (HasStoredAssignment(actor)) {
        VirtualSlots::RequestVisualRefresh(actor);
    }

    if (IsManagedActor(actor)) {
        QueueRefresh(actor, RefreshReason::kLoad3D);
    }
}

void Revert() {
    std::scoped_lock lock(g_lock);
    g_followerTradeActor = std::nullopt;
    RegisteredActors().clear();
    RefreshStates().clear();
}
}
