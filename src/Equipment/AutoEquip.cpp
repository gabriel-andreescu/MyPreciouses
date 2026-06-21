#include "Equipment/AutoEquip.h"

#include "Compatibility/Vanilla.h"
#include "Equipment/AssignmentStore.h"
#include "Inventory.h"
#include "Settings.h"
#include "SourceModelFootprints.h"
#include "UI.h"
#include "VirtualSlots.h"

#include <RE/B/BGSEquipSlot.h>
#include <RE/C/ContainerMenu.h>
#include <RE/E/ExtraCannotWear.h>
#include <RE/E/ExtraDataTypes.h>
#include <RE/M/Misc.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <memory>
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
    constexpr RE::FormID kRightHandEquipSlotFormID {0x00013F42};
    constexpr Core::Target kNpcBondOfMatrimonyLeftRingFingerTarget {
        .hand = Core::Hand::kLeft,
        .finger = Core::Finger::kRing,
    };
    constexpr std::array kNpcAutoEquipTargetPriority {
        Core::Target {.hand = Core::Hand::kRight, .finger = Core::Finger::kIndex},
        Core::Target {.hand = Core::Hand::kRight, .finger = Core::Finger::kMiddle},
        Core::Target {.hand = Core::Hand::kRight, .finger = Core::Finger::kRing},
        Core::Target {.hand = Core::Hand::kRight, .finger = Core::Finger::kPinky},
        Core::Target {.hand = Core::Hand::kRight, .finger = Core::Finger::kThumb},
        Core::Target {.hand = Core::Hand::kLeft, .finger = Core::Finger::kIndex},
        Core::Target {.hand = Core::Hand::kLeft, .finger = Core::Finger::kMiddle},
        Core::Target {.hand = Core::Hand::kLeft, .finger = Core::Finger::kRing},
        Core::Target {.hand = Core::Hand::kLeft, .finger = Core::Finger::kPinky},
        Core::Target {.hand = Core::Hand::kLeft, .finger = Core::Finger::kThumb},
    };
    static_assert(kNpcAutoEquipTargetPriority.size() == Core::kAllTargets.size());

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
        RE::ExtraDataList* sourceExtraList {nullptr};
        Core::Target target;
    };

    struct NativeRingSlotApplyResult {
        bool changed {false};
        bool failed {false};
        std::optional<Inventory::RightWornRing> fallbackReservedNativeRing;
        std::vector<RE::ExtraDataList*> removedCannotWearExtraLists;
    };

    struct NativeRingSlotClearResult {
        bool changed {false};
        bool cleared {false};
        bool failed {false};
        std::vector<RE::ExtraDataList*> removedCannotWearExtraLists;
    };

    enum class NativeRingSlotPlanState : std::uint8_t {
        kNone,
        kResolved,
        kResolutionFailed,
    };

    struct NativeRingSlotPlan {
        NativeRingSlotPlanState state {NativeRingSlotPlanState::kNone};
        std::optional<AppliedAssignment> assignment;
    };

    struct PlanConstraints {
        Core::TargetMask preoccupiedTargets;
        bool allowNativeRingSlot {true};
        std::optional<Inventory::RightWornRing> reservedNativeRing;
    };

    struct NativeRingSlotClearPolicy {
        bool allowCannotWearBondOfMatrimonyRelocation {false};
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

    [[nodiscard]] bool CanAutoEquipActor(const Core::ActorKey a_actor) {
        return a_actor && !Core::IsPlayerActorKey(a_actor) && Settings::GetSingleton()->IsNpcSupportEnabled();
    }

    [[nodiscard]] bool RegisterActor(const Core::ActorKey a_actor) {
        if (!CanAutoEquipActor(a_actor)) {
            return false;
        }

        std::scoped_lock lock(g_lock);
        return RegisteredActors().insert(a_actor).second;
    }

    [[nodiscard]] constexpr ReasonMask ToReasonMask(const RefreshReason a_reason) {
        return static_cast<ReasonMask>(a_reason);
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
        if (!actor) {
            return std::nullopt;
        }

        const auto actorKey = Core::MakeActorKey(*actor);
        if (!CanAutoEquipActor(actorKey) || !actor->IsPlayerTeammate()) {
            return std::nullopt;
        }

        return actorKey;
    }

    void SetFollowerTradeActor(const std::optional<Core::ActorKey> a_actor) {
        std::scoped_lock lock(g_lock);
        g_followerTradeActor = a_actor;
        if (a_actor) {
            RegisteredActors().insert(*a_actor);
        }
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
            const auto available = std::max(matches.count, 0);
            return a_source.extraUniqueID ? static_cast<std::uint32_t>(available)
                                          : static_cast<std::uint32_t>(std::min(available, 1));
        }

        if (a_source.IsFormOnly()) {
            const auto matches = Inventory::FindFormOnlySourceMatches(a_actor, a_ring);
            return static_cast<std::uint32_t>(std::max(matches.count, 0));
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

            const auto sourceTargets = SourceModelFootprints::GetRingGeometrySourceTargets(*source->ring);
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

    [[nodiscard]] bool ShouldApplyBondOfMatrimonyLeftRingFingerPreference(
        const Core::ActorKey a_actor,
        const bool a_hasBondCandidate
    ) {
        return a_hasBondCandidate
               && CanAutoEquipActor(a_actor)
               && Settings::GetSingleton()->ShouldNpcPreferBondOfMatrimonyOnLeftRingFinger();
    }

    [[nodiscard]] bool HasBondOfMatrimonyCandidate(const std::vector<Candidate>& a_candidates) {
        return std::ranges::any_of(a_candidates, [](const Candidate& a_candidate) {
            return Compatibility::Vanilla::IsBondOfMatrimony(a_candidate.ring);
        });
    }

    [[nodiscard]] bool ReservedNativeRingConsumesCandidateCopy(
        const std::optional<Inventory::RightWornRing>& a_reservedNativeRing,
        const Candidate& a_candidate
    ) {
        if (!a_reservedNativeRing || !a_candidate.ring) {
            return false;
        }

        return Inventory::RightWornRingMatchesSource(*a_reservedNativeRing, *a_candidate.ring, a_candidate.source);
    }

    [[nodiscard]] std::uint32_t EffectiveAvailableCopies(
        const Candidate& a_candidate,
        const std::optional<Inventory::RightWornRing>& a_reservedNativeRing
    ) {
        if (!ReservedNativeRingConsumesCandidateCopy(a_reservedNativeRing, a_candidate)) {
            return a_candidate.availableCopies;
        }

        return a_candidate.availableCopies > 0 ? a_candidate.availableCopies - 1 : 0;
    }

    [[nodiscard]] bool IsBetterCandidate(
        const Candidate& a_lhs,
        const Candidate& a_rhs,
        const Core::Target a_target,
        const bool a_prioritizeBondOfMatrimony
    ) {
        if (a_prioritizeBondOfMatrimony) {
            const auto lhsBond = Compatibility::Vanilla::IsBondOfMatrimony(a_lhs.ring);
            const auto rhsBond = Compatibility::Vanilla::IsBondOfMatrimony(a_rhs.ring);
            if (lhsBond != rhsBond) {
                return lhsBond;
            }
        }

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
        const Core::TargetMask& a_occupiedTargets,
        const bool a_prioritizeBondOfMatrimony,
        const std::optional<Inventory::RightWornRing>& a_reservedNativeRing
    ) {
        std::optional<std::size_t> bestIndex;
        for (std::size_t index = 0; index < a_candidates.size(); ++index) {
            const auto& candidate = a_candidates[index];
            const auto availableCopies = EffectiveAvailableCopies(candidate, a_reservedNativeRing);
            if (a_usedCopies[index] >= availableCopies || !CanPlaceCandidate(candidate, a_target, a_occupiedTargets)) {
                continue;
            }

            if (!bestIndex
                || IsBetterCandidate(candidate, a_candidates[*bestIndex], a_target, a_prioritizeBondOfMatrimony)) {
                bestIndex = index;
            }
        }

        return bestIndex;
    }

    [[nodiscard]] std::array<Core::Target, Core::kAllTargets.size()> BuildTargetOrder(
        const bool a_preferBondOnLeftRingFinger
    ) {
        auto targets = kNpcAutoEquipTargetPriority;
        if (!a_preferBondOnLeftRingFinger) {
            return targets;
        }

        const auto preferred = std::ranges::find(targets, kNpcBondOfMatrimonyLeftRingFingerTarget);
        if (preferred != targets.end()) {
            std::rotate(targets.begin(), preferred, std::next(preferred));
        }
        return targets;
    }

    [[nodiscard]] std::vector<PlannedAssignment> BuildPlan(
        RE::Actor& a_actor,
        const std::vector<Candidate>& a_candidates,
        const PlanConstraints& a_constraints = {}
    ) {
        std::vector<PlannedAssignment> plan;
        plan.reserve(Core::kAllTargets.size());

        auto occupiedTargets = a_constraints.preoccupiedTargets;
        std::vector<std::uint32_t> usedCopies(a_candidates.size(), 0);
        const auto hasBondCandidate = HasBondOfMatrimonyCandidate(a_candidates);
        const auto preferBondOnLeftRingFinger = ShouldApplyBondOfMatrimonyLeftRingFingerPreference(
            Core::MakeActorKey(a_actor),
            hasBondCandidate
        );
        const auto targetOrder = BuildTargetOrder(preferBondOnLeftRingFinger);

        for (const auto target : targetOrder) {
            if (!a_constraints.allowNativeRingSlot && target == Core::kVanillaRingSlotTarget) {
                continue;
            }

            const auto candidateIndex = SelectCandidateForTarget(
                a_candidates,
                usedCopies,
                target,
                occupiedTargets,
                preferBondOnLeftRingFinger && target == kNpcBondOfMatrimonyLeftRingFingerTarget,
                a_constraints.reservedNativeRing
            );
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

    [[nodiscard]] std::optional<Core::ActorKey> EnsureManagedActorWithCandidates(RE::Actor& a_actor) {
        const auto actor = Core::MakeActorKey(a_actor);
        if (!CanAutoEquipActor(actor)) {
            return std::nullopt;
        }

        if (IsManagedActor(actor)) {
            return actor;
        }

        const auto candidates = CollectCandidates(a_actor);
        if (candidates.empty()) {
            return std::nullopt;
        }

        const auto registered = RegisterActor(actor);
        logger::debug(
            "AutoEquip: actor registered | actor={:08X} | candidates={} | registered={}",
            actor.referenceFormID,
            candidates.size(),
            registered
        );
        return actor;
    }

    void QueueRefreshIfManagedOrCandidate(RE::Actor& a_actor, const RefreshReason a_reason) {
        if (const auto actor = EnsureManagedActorWithCandidates(a_actor)) {
            QueueRefresh(*actor, a_reason);
        }
    }

    void QueueContainerActorRefresh(
        const RE::FormID a_container,
        const RefreshReason a_reason,
        const bool a_allowCandidateRegistration
    ) {
        auto* actor = a_container ? RE::TESForm::LookupByID<RE::Actor>(a_container) : nullptr;
        if (!actor) {
            return;
        }

        const auto actorKey = Core::MakeActorKey(*actor);
        if (!CanAutoEquipActor(actorKey)) {
            return;
        }

        if (IsManagedActor(actorKey)) {
            QueueRefresh(actorKey, a_reason);
            return;
        }

        if (a_allowCandidateRegistration) {
            QueueRefreshIfManagedOrCandidate(*actor, a_reason);
        }
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

    [[nodiscard]] std::optional<PlannedAssignment> FindPlannedAssignment(
        const std::vector<PlannedAssignment>& a_plan,
        const Core::Target a_target
    ) {
        const auto it = std::ranges::find(a_plan, a_target, &PlannedAssignment::target);
        return it != a_plan.end() ? std::make_optional(*it) : std::nullopt;
    }

    [[nodiscard]] bool PlanSelectsBondOfMatrimonyForLeftRingFinger(
        const Core::ActorKey a_actor,
        const std::vector<Candidate>& a_candidates,
        const std::vector<PlannedAssignment>& a_plan
    ) {
        if (!CanAutoEquipActor(a_actor)
            || !Settings::GetSingleton()->ShouldNpcPreferBondOfMatrimonyOnLeftRingFinger()) {
            return false;
        }

        const auto plannedLeftRing = FindPlannedAssignment(a_plan, kNpcBondOfMatrimonyLeftRingFingerTarget);
        if (!plannedLeftRing || plannedLeftRing->candidateIndex >= a_candidates.size()) {
            return false;
        }

        return Compatibility::Vanilla::IsBondOfMatrimony(a_candidates[plannedLeftRing->candidateIndex].ring);
    }

    [[nodiscard]] RE::ExtraDataList* ResolveCustomSourceExtraList(
        RE::Actor& a_actor,
        const Candidate& a_candidate,
        const Core::ItemSource& a_source
    ) {
        if (!a_candidate.ring || !a_source.IsCustomEnchantment()) {
            return nullptr;
        }

        if (a_candidate.sourceExtraList
            && Inventory::MatchesCustomSelection(
                a_candidate.sourceExtraList,
                a_source.customEnchantment,
                a_source.extraUniqueID
            )) {
            return a_candidate.sourceExtraList;
        }

        const auto matches = Inventory::FindCustomSourceMatches(
            a_actor,
            *a_candidate.ring,
            a_source.customEnchantment,
            a_source.extraUniqueID
        );
        return matches.firstExtraList;
    }

    [[nodiscard]] std::optional<AppliedAssignment> ResolveAppliedAssignment(
        RE::Actor& a_actor,
        const std::vector<Candidate>& a_candidates,
        const PlannedAssignment& a_planned
    ) {
        if (a_planned.candidateIndex >= a_candidates.size()) {
            return std::nullopt;
        }

        const auto& candidate = a_candidates[a_planned.candidateIndex];
        auto source = ResolveAppliedSource(a_actor, candidate);
        if (!source || !source->IsAssigned() || !candidate.ring) {
            return std::nullopt;
        }

        auto* sourceExtraList = ResolveCustomSourceExtraList(a_actor, candidate, *source);
        if (source->IsCustomEnchantment() && !sourceExtraList) {
            logger::warn(
                "AutoEquip: custom ring skipped | actor={:08X} | source={:08X} | target={} | reason=sourceExtraListUnavailable",
                a_actor.GetFormID(),
                source->sourceFormID,
                Core::TargetName(a_planned.target)
            );
            return std::nullopt;
        }

        return AppliedAssignment {
            .ring = candidate.ring,
            .source = std::move(*source),
            .sourceExtraList = sourceExtraList,
            .target = a_planned.target,
        };
    }

    [[nodiscard]] std::array<std::optional<AppliedAssignment>, Core::kAllTargets.size()> ResolveAppliedVirtualPlan(
        RE::Actor& a_actor,
        const std::vector<Candidate>& a_candidates,
        const std::vector<PlannedAssignment>& a_plan
    ) {
        std::array<std::optional<AppliedAssignment>, Core::kAllTargets.size()> appliedPlan;
        for (const auto& planned : a_plan) {
            if (planned.target == Core::kVanillaRingSlotTarget) {
                continue;
            }

            appliedPlan[Core::ToIndex(planned.target)] = ResolveAppliedAssignment(a_actor, a_candidates, planned);
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

    [[nodiscard]] bool StoredAssignmentMatches(const Core::ActorKey a_actor, const AppliedAssignment& a_assignment) {
        const auto current = AssignmentStore::Get(a_actor, a_assignment.target);
        return current.IsAssigned() && current.source == a_assignment.source;
    }

    [[nodiscard]] bool CanRemoveCannotWearForBondOfMatrimonyRelocation(
        const Inventory::RightWornRing& a_rightWorn,
        const NativeRingSlotClearPolicy& a_clearPolicy
    ) {
        return a_clearPolicy.allowCannotWearBondOfMatrimonyRelocation
               && Compatibility::Vanilla::IsBondOfMatrimony(a_rightWorn.ring)
               && a_rightWorn.extraList
               && a_rightWorn.extraList->HasType(RE::ExtraDataType::kCannotWear)
               && !a_rightWorn.extraList->HasQuestObjectAlias();
    }

    void MarkInventoryChanged(RE::Actor& a_actor) {
        if (auto* inventoryChanges = a_actor.GetInventoryChanges()) {
            inventoryChanges->changed = true;
        }
    }

    void TrackRemovedCannotWear(
        std::vector<RE::ExtraDataList*>& a_removedCannotWearExtraLists,
        RE::ExtraDataList& a_extraList
    ) {
        if (std::ranges::find(a_removedCannotWearExtraLists, std::addressof(a_extraList))
            == a_removedCannotWearExtraLists.end()) {
            a_removedCannotWearExtraLists.push_back(std::addressof(a_extraList));
        }
    }

    [[nodiscard]] bool RestoreCannotWear(RE::ExtraDataList& a_extraList) {
        if (a_extraList.HasType(RE::ExtraDataType::kCannotWear)) {
            return true;
        }

        auto* cannotWear = RE::BSExtraData::Create<RE::ExtraCannotWear>();
        if (!cannotWear) {
            return false;
        }

        a_extraList.Add(cannotWear);
        return a_extraList.HasType(RE::ExtraDataType::kCannotWear);
    }

    void RestoreRemovedCannotWear(
        RE::Actor& a_actor,
        const std::vector<RE::ExtraDataList*>& a_removedCannotWearExtraLists
    ) {
        auto restoredAny = false;
        for (auto* extraList : a_removedCannotWearExtraLists) {
            if (!extraList) {
                continue;
            }

            if (RestoreCannotWear(*extraList)) {
                restoredAny = true;
                continue;
            }

            logger::warn(
                "AutoEquip: Bond of Matrimony protection restore failed | actor={:08X} | extraList={}",
                a_actor.GetFormID(),
                static_cast<const void*>(extraList)
            );
        }

        if (restoredAny) {
            MarkInventoryChanged(a_actor);
        }
    }

    [[nodiscard]] bool PrepareNativeRingSlotClearForAutoEquip(
        RE::Actor& a_actor,
        const Inventory::RightWornRing& a_rightWorn,
        const NativeRingSlotClearPolicy& a_clearPolicy,
        std::vector<RE::ExtraDataList*>& a_removedCannotWearExtraLists
    ) {
        if (!a_rightWorn.protectedStack) {
            return true;
        }

        if (!a_rightWorn.extraList) {
            return false;
        }

        if (!CanRemoveCannotWearForBondOfMatrimonyRelocation(a_rightWorn, a_clearPolicy)) {
            logger::debug(
                "AutoEquip: right ring retained | actor={:08X} | source={:08X} | reason=protectedRightRing",
                a_actor.GetFormID(),
                a_rightWorn.ring ? a_rightWorn.ring->GetFormID() : 0
            );
            return false;
        }

        auto& extraList = *a_rightWorn.extraList;
        if (!extraList.RemoveByType(RE::ExtraDataType::kCannotWear)) {
            logger::warn(
                "AutoEquip: right ring retained | actor={:08X} | source={:08X} | reason=protectedRightRing",
                a_actor.GetFormID(),
                a_rightWorn.ring ? a_rightWorn.ring->GetFormID() : 0
            );
            return false;
        }

        TrackRemovedCannotWear(a_removedCannotWearExtraLists, extraList);
        MarkInventoryChanged(a_actor);
        logger::debug(
            "AutoEquip: Bond of Matrimony protection removed for left-ring relocation | actor={:08X} | source={:08X}",
            a_actor.GetFormID(),
            a_rightWorn.ring ? a_rightWorn.ring->GetFormID() : 0
        );

        return true;
    }

    [[nodiscard]] NativeRingSlotClearResult ClearNativeRingSlotForAutoEquip(
        RE::Actor& a_actor,
        const NativeRingSlotClearPolicy& a_clearPolicy
    ) {
        constexpr auto kMaxRightWornRingUnequipAttempts = std::uint8_t {10};

        NativeRingSlotClearResult result;
        for (auto attempt = std::uint8_t {0}; attempt < kMaxRightWornRingUnequipAttempts; ++attempt) {
            const auto rightWorn = Inventory::FindRightWornRing(a_actor);
            if (!rightWorn) {
                result.cleared = true;
                return result;
            }

            if (!rightWorn->ring) {
                result.failed = true;
                RestoreRemovedCannotWear(a_actor, result.removedCannotWearExtraLists);
                return result;
            }

            auto* equipManager = RE::ActorEquipManager::GetSingleton();
            if (!equipManager) {
                result.failed = true;
                RestoreRemovedCannotWear(a_actor, result.removedCannotWearExtraLists);
                return result;
            }

            if (!PrepareNativeRingSlotClearForAutoEquip(
                    a_actor,
                    *rightWorn,
                    a_clearPolicy,
                    result.removedCannotWearExtraLists
                )) {
                result.failed = true;
                RestoreRemovedCannotWear(a_actor, result.removedCannotWearExtraLists);
                return result;
            }

            auto* unequippedRing = rightWorn->ring;
            auto* unequippedExtraList = rightWorn->extraList;
            equipManager->UnequipObject(
                std::addressof(a_actor),
                rightWorn->ring,
                rightWorn->extraList,
                1,
                nullptr,
                true,
                false,
                false,
                true,
                nullptr
            );

            result.changed = true;
            const auto nextRightWorn = Inventory::FindRightWornRing(a_actor);
            const auto sameRing = nextRightWorn && nextRightWorn->ring == unequippedRing;
            const auto sameExtraList = nextRightWorn && nextRightWorn->extraList == unequippedExtraList;
            if (sameRing && sameExtraList) {
                logger::warn(
                    "AutoEquip: right ring unequip failed | actor={:08X} | source={:08X}",
                    a_actor.GetFormID(),
                    unequippedRing->GetFormID()
                );
                result.failed = true;
                RestoreRemovedCannotWear(a_actor, result.removedCannotWearExtraLists);
                return result;
            }
        }

        result.cleared = !Inventory::FindRightWornRing(a_actor).has_value();
        result.failed = !result.cleared;
        if (result.failed) {
            RestoreRemovedCannotWear(a_actor, result.removedCannotWearExtraLists);
        }
        return result;
    }

    [[nodiscard]] bool NativeRingSlotMatchesAssignment(
        const std::optional<Inventory::RightWornRing>& a_rightWorn,
        const AppliedAssignment& a_assignment
    ) {
        if (!a_rightWorn || !a_assignment.ring) {
            return false;
        }

        return Inventory::RightWornRingMatchesSource(*a_rightWorn, *a_assignment.ring, a_assignment.source);
    }

    [[nodiscard]] bool NativeRingSlotMatchesRightWornRing(
        const std::optional<Inventory::RightWornRing>& a_current,
        const Inventory::RightWornRing& a_expected
    ) {
        return a_current && a_current->ring == a_expected.ring && a_current->extraList == a_expected.extraList;
    }

    [[nodiscard]] bool EquipNativeRingSlotAssignment(RE::Actor& a_actor, const AppliedAssignment& a_assignment) {
        if (!a_assignment.ring) {
            return false;
        }

        auto* equipManager = RE::ActorEquipManager::GetSingleton();
        auto* equipSlot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(kRightHandEquipSlotFormID);
        if (!equipManager || !equipSlot) {
            return false;
        }

        equipManager->EquipObject(
            std::addressof(a_actor),
            a_assignment.ring,
            a_assignment.sourceExtraList,
            1,
            equipSlot,
            true,
            false,
            false,
            true
        );

        return NativeRingSlotMatchesAssignment(Inventory::FindRightWornRing(a_actor), a_assignment);
    }

    [[nodiscard]] bool RestoreNativeRingSlot(RE::Actor& a_actor, const Inventory::RightWornRing& a_original) {
        if (!a_original.ring) {
            return false;
        }

        if (NativeRingSlotMatchesRightWornRing(Inventory::FindRightWornRing(a_actor), a_original)) {
            return true;
        }

        const auto clearResult = ClearNativeRingSlotForAutoEquip(a_actor, NativeRingSlotClearPolicy {});
        if (clearResult.failed) {
            logger::warn(
                "AutoEquip: right ring restore skipped | actor={:08X} | source={:08X} | reason=clearCurrentFailed",
                a_actor.GetFormID(),
                a_original.ring->GetFormID()
            );
            return false;
        }

        auto* equipManager = RE::ActorEquipManager::GetSingleton();
        auto* equipSlot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(kRightHandEquipSlotFormID);
        if (!equipManager || !equipSlot) {
            return false;
        }

        equipManager->EquipObject(
            std::addressof(a_actor),
            a_original.ring,
            a_original.extraList,
            1,
            equipSlot,
            true,
            false,
            false,
            true
        );

        const auto restored = NativeRingSlotMatchesRightWornRing(Inventory::FindRightWornRing(a_actor), a_original);
        if (!restored) {
            logger::warn(
                "AutoEquip: right ring restore failed | actor={:08X} | source={:08X}",
                a_actor.GetFormID(),
                a_original.ring->GetFormID()
            );
        }
        return restored;
    }

    [[nodiscard]] NativeRingSlotApplyResult ApplyNativeRingSlotPlan(
        RE::Actor& a_actor,
        const std::optional<AppliedAssignment>& a_assignment,
        const NativeRingSlotClearPolicy& a_clearPolicy
    ) {
        NativeRingSlotApplyResult result;
        const auto originalRightWorn = Inventory::FindRightWornRing(a_actor);
        if (a_assignment && NativeRingSlotMatchesAssignment(originalRightWorn, *a_assignment)) {
            return result;
        }

        if (originalRightWorn) {
            const auto clearResult = ClearNativeRingSlotForAutoEquip(a_actor, a_clearPolicy);
            result.changed = result.changed || clearResult.changed;
            result.removedCannotWearExtraLists = clearResult.removedCannotWearExtraLists;
            if (clearResult.failed) {
                result.failed = true;
                result.fallbackReservedNativeRing = originalRightWorn;
                return result;
            }
        }

        if (!a_assignment) {
            return result;
        }

        if (!EquipNativeRingSlotAssignment(a_actor, *a_assignment)) {
            logger::warn(
                "AutoEquip: right ring equip failed | actor={:08X} | source={:08X}",
                a_actor.GetFormID(),
                a_assignment->source.sourceFormID
            );
            RestoreRemovedCannotWear(a_actor, result.removedCannotWearExtraLists);
            if (originalRightWorn) {
                static_cast<void>(RestoreNativeRingSlot(a_actor, *originalRightWorn));
                result.fallbackReservedNativeRing = originalRightWorn;
            }
            result.failed = true;
            return result;
        }

        result.changed = true;
        return result;
    }

    [[nodiscard]] Core::TargetMask GetNativeRingSlotOccupiedTargets(
        const std::optional<Inventory::RightWornRing>& a_rightWorn
    ) {
        Core::TargetMask occupiedTargets;
        if (a_rightWorn && a_rightWorn->ring) {
            AddTargets(
                occupiedTargets,
                SourceModelFootprints::GetProjectedRingGeometryTargets(*a_rightWorn->ring, Core::kVanillaRingSlotTarget)
            );
        }
        return occupiedTargets;
    }

    [[nodiscard]] std::vector<PlannedAssignment> BuildPlanWithNativeRingSlotReserved(
        RE::Actor& a_actor,
        const std::vector<Candidate>& a_candidates,
        const std::optional<Inventory::RightWornRing>& a_reservedNativeRing
    ) {
        return BuildPlan(
            a_actor,
            a_candidates,
            PlanConstraints {
                .preoccupiedTargets = GetNativeRingSlotOccupiedTargets(a_reservedNativeRing),
                .allowNativeRingSlot = false,
                .reservedNativeRing = a_reservedNativeRing,
            }
        );
    }

    [[nodiscard]] std::vector<PlannedAssignment> BuildPlanWithCurrentNativeRingSlotReserved(
        RE::Actor& a_actor,
        const std::vector<Candidate>& a_candidates
    ) {
        return BuildPlanWithNativeRingSlotReserved(a_actor, a_candidates, Inventory::FindRightWornRing(a_actor));
    }

    [[nodiscard]] NativeRingSlotPlan ResolveNativeRingSlotPlan(
        RE::Actor& a_actor,
        const std::vector<Candidate>& a_candidates,
        const std::vector<PlannedAssignment>& a_plan
    ) {
        const auto plannedNativeSlot = FindPlannedAssignment(a_plan, Core::kVanillaRingSlotTarget);
        if (!plannedNativeSlot) {
            return {};
        }

        auto assignment = ResolveAppliedAssignment(a_actor, a_candidates, *plannedNativeSlot);
        if (!assignment) {
            return NativeRingSlotPlan {.state = NativeRingSlotPlanState::kResolutionFailed};
        }

        return NativeRingSlotPlan {
            .state = NativeRingSlotPlanState::kResolved,
            .assignment = std::move(assignment),
        };
    }

    [[nodiscard]] bool ApplyPlan(
        const Core::ActorKey a_actor,
        RE::Actor& a_actorRef,
        const std::vector<Candidate>& a_candidates,
        const std::vector<PlannedAssignment>& a_plan
    ) {
        auto changed = false;
        const auto nativeSlotClearPolicy = NativeRingSlotClearPolicy {
            .allowCannotWearBondOfMatrimonyRelocation = PlanSelectsBondOfMatrimonyForLeftRingFinger(
                a_actor,
                a_candidates,
                a_plan
            ),
        };
        const auto nativeSlotPlan = ResolveNativeRingSlotPlan(a_actorRef, a_candidates, a_plan);
        const auto nativeSlotResult = nativeSlotPlan.state == NativeRingSlotPlanState::kResolutionFailed
                                          ? NativeRingSlotApplyResult {.failed = true}
                                          : ApplyNativeRingSlotPlan(
                                                a_actorRef,
                                                nativeSlotPlan.assignment,
                                                nativeSlotClearPolicy
                                            );
        changed = changed || nativeSlotResult.changed;

        auto virtualPlan = a_plan;
        if (nativeSlotResult.failed) {
            virtualPlan = nativeSlotResult.fallbackReservedNativeRing
                              ? BuildPlanWithNativeRingSlotReserved(
                                    a_actorRef,
                                    a_candidates,
                                    nativeSlotResult.fallbackReservedNativeRing
                                )
                              : BuildPlanWithCurrentNativeRingSlotReserved(a_actorRef, a_candidates);
        }

        const auto appliedPlan = ResolveAppliedVirtualPlan(a_actorRef, a_candidates, virtualPlan);
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

        if (!nativeSlotResult.removedCannotWearExtraLists.empty()
            && nativeSlotClearPolicy.allowCannotWearBondOfMatrimonyRelocation) {
            const auto leftRingIndex = Core::ToIndex(kNpcBondOfMatrimonyLeftRingFingerTarget);
            const auto& desiredLeftRing = appliedPlan[leftRingIndex];
            if (!desiredLeftRing || !StoredAssignmentMatches(a_actor, *desiredLeftRing)) {
                RestoreRemovedCannotWear(a_actorRef, nativeSlotResult.removedCannotWearExtraLists);
            }
        }

        return changed;
    }

    [[nodiscard]] bool ShouldRefreshWhenUnchanged(const ReasonMask a_reasons) {
        constexpr auto refreshReasons = ToReasonMask(RefreshReason::kLoad)
                                        | ToReasonMask(RefreshReason::kSettingsChanged)
                                        | ToReasonMask(RefreshReason::kLoad3D);
        return (a_reasons & refreshReasons) != 0;
    }

    [[nodiscard]] VirtualSlots::RefreshOptions ToVirtualRefreshOptions(const ReasonMask a_reasons) {
        return VirtualSlots::RefreshOptions {
            .preserveLoadedEffects = (a_reasons & ToReasonMask(RefreshReason::kLoad)) != 0,
            .reapplyEffects = (a_reasons & ToReasonMask(RefreshReason::kSettingsChanged)) != 0,
        };
    }

    void RunRefresh(const Core::ActorKey a_actor, const ReasonMask a_reasons) {
        if (!IsManagedActor(a_actor)) {
            return;
        }

        auto* actor = Core::ResolveActor(a_actor);
        if (!actor) {
            return;
        }

        const auto candidates = CollectCandidates(*actor);
        const auto plan = BuildPlan(*actor, candidates);
        const auto changed = ApplyPlan(a_actor, *actor, candidates, plan);
        if (changed || ShouldRefreshWhenUnchanged(a_reasons)) {
            VirtualSlots::RequestRefresh(a_actor, ToVirtualRefreshOptions(a_reasons));
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
    return CanAutoEquipActor(a_actor)
           && (MatchesTradeActor(a_actor) || IsRegisteredActor(a_actor) || HasStoredAssignment(a_actor));
}

void QueueRefresh(const Core::ActorKey a_actor, const RefreshReason a_reason) {
    if (!IsManagedActor(a_actor)) {
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

void QueueRefreshKnownActors(const RefreshReason a_reason) {
    const auto snapshots = AssignmentStore::GetAllSnapshots();
    std::unordered_set<Core::ActorKey> actors;
    for (const auto& snapshot : snapshots) {
        if (Core::IsPlayerActorKey(snapshot.actor)) {
            if (a_reason != RefreshReason::kAutoEquipPlanRulesChanged) {
                VirtualSlots::RequestRefresh(snapshot.actor, ToVirtualRefreshOptions(ToReasonMask(a_reason)));
            }
            continue;
        }

        actors.insert(snapshot.actor);
    }

    {
        std::scoped_lock lock(g_lock);
        actors.insert(RegisteredActors().begin(), RegisteredActors().end());
        if (g_followerTradeActor) {
            actors.insert(*g_followerTradeActor);
        }
    }

    for (const auto actor : actors) {
        QueueRefresh(actor, a_reason);
    }
}

void HandleContainerChanged(const RE::TESContainerChangedEvent& a_event) {
    if (!Inventory::AsRing(RE::TESForm::LookupByID(a_event.baseObj))) {
        return;
    }

    if (!Settings::GetSingleton()->IsNpcSupportEnabled()) {
        return;
    }

    QueueContainerActorRefresh(a_event.oldContainer, RefreshReason::kContainerChanged, false);
    if (a_event.newContainer != a_event.oldContainer) {
        QueueContainerActorRefresh(a_event.newContainer, RefreshReason::kContainerChanged, true);
    }
}

void HandleEquipEvent(RE::Actor& a_actor, const RE::FormID a_sourceFormID) {
    if (!Inventory::AsRing(RE::TESForm::LookupByID(a_sourceFormID))) {
        return;
    }

    const auto actor = Core::MakeActorKey(a_actor);
    if (IsManagedActor(actor)) {
        QueueRefresh(actor, RefreshReason::kEquipChanged);
        return;
    }

    QueueRefreshIfManagedOrCandidate(a_actor, RefreshReason::kEquipChanged);
}

void HandleActorLoad3D(RE::Actor& a_actor) {
    const auto actor = Core::MakeActorKey(a_actor);
    if (!CanAutoEquipActor(actor)) {
        return;
    }

    if (HasStoredAssignment(actor)) {
        VirtualSlots::RequestVisualRefresh(actor);
    }

    QueueRefreshIfManagedOrCandidate(a_actor, RefreshReason::kLoad3D);
}

void Revert() {
    std::scoped_lock lock(g_lock);
    g_followerTradeActor = std::nullopt;
    RegisteredActors().clear();
    RefreshStates().clear();
}
}
