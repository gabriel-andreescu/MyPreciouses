#include "Equipment/AssignmentActions.h"

#include "Audio/EquipSounds.h"
#include "Equipment/AssignmentStore.h"
#include "Inventory.h"
#include "Settings.h"
#include "SourceModelFootprints.h"
#include "VirtualSlots.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace Equipment {
namespace {
    constexpr RE::FormID kRightHandEquipSlotFormID {0x00013F42};
    constexpr RE::FormID kLeftHandEquipSlotFormID {0x00013F43};

    [[nodiscard]] RE::TESObjectARMO* LookupSourceRing(const RE::FormID a_sourceFormID) {
        return Inventory::AsRing(RE::TESForm::LookupByID(a_sourceFormID));
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

    [[nodiscard]] bool CanUseEnabledTargets(
        const Core::TargetMask& a_occupiedTargets,
        const Core::Target a_target,
        const RE::TESObjectARMO& a_ring,
        const std::string_view a_action
    ) {
        if (Settings::GetSingleton()->AreTargetsEnabled(a_occupiedTargets)) {
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

    [[nodiscard]] ActionResult RightHandRingCannotBeUnequippedResult();

    [[nodiscard]] bool ClearVirtualAssignment(RE::Actor& a_actor, const Core::Target a_target) {
        const auto actorKey = Core::MakeActorKey(a_actor);
        const auto hadAssignment = AssignmentStore::Get(actorKey, a_target).IsAssigned();
        AssignmentStore::Clear(actorKey, a_target);
        VirtualSlots::ClearTarget(actorKey, a_target);
        return hadAssignment;
    }

    struct SourceMatch {
        RE::ExtraDataList* equipExtraList {nullptr};
        RE::ExtraDataList* rightWornExtraList {nullptr};
        std::int32_t count {0};
        bool rightWorn {false};
        bool rightWornProtected {false};

        [[nodiscard]] bool HasMatch() const {
            return count > 0;
        }
    };

    struct VirtualSourceTarget {
        Core::Target target;
        Core::ItemSource source;
    };

    [[nodiscard]] std::optional<Core::ItemSource> GetItemSource(const Core::Assignment& a_assignment) {
        return a_assignment.source.IsAssigned() ? std::make_optional(a_assignment.source) : std::nullopt;
    }

    [[nodiscard]] SourceMatch FindSourceMatch(
        RE::Actor& a_actor,
        const RE::TESObjectARMO& a_ring,
        const Core::ItemSource& a_source
    ) {
        if (a_source.IsCustomEnchantment()) {
            const auto sourceMatches = Inventory::FindCustomSourceMatches(
                a_actor,
                a_ring,
                a_source.customEnchantment,
                a_source.extraUniqueID
            );
            return SourceMatch {
                .equipExtraList = sourceMatches.firstExtraList,
                .rightWornExtraList = sourceMatches.rightWornExtraList,
                .count = sourceMatches.count,
                .rightWorn = sourceMatches.rightWornExtraList != nullptr,
                .rightWornProtected = sourceMatches.rightWornProtected,
            };
        }

        const auto sourceMatches = Inventory::FindFormOnlySourceMatches(a_actor, a_ring);
        return SourceMatch {
            .rightWornExtraList = sourceMatches.rightWornExtraList,
            .count = sourceMatches.count,
            .rightWorn = sourceMatches.rightWorn,
            .rightWornProtected = sourceMatches.rightWornProtected,
        };
    }

    [[nodiscard]] std::uint32_t CountSelectedVirtualCopies(
        const Core::ActorKey a_actor,
        const Core::ItemSource& a_source,
        const std::optional<Core::Target> a_excludedTarget = std::nullopt
    ) {
        if (!a_source.IsAssigned()) {
            return 0;
        }

        return AssignmentStore::CountMatching(a_actor, a_source, a_excludedTarget);
    }

    [[nodiscard]] bool RightSlotConsumesNeededCopy(
        const SourceMatch& a_match,
        const std::uint32_t a_selectedVirtualCopies
    ) {
        return a_match.rightWorn && std::cmp_less_equal(a_match.count, a_selectedVirtualCopies + 1);
    }

    [[nodiscard]] bool HasNoFreeVirtualCopy(const SourceMatch& a_match, const std::uint32_t a_selectedVirtualCopies) {
        return !a_match.rightWorn && std::cmp_less_equal(a_match.count, a_selectedVirtualCopies);
    }

    [[nodiscard]] bool HasSpareCopyAfterVanillaEquip(
        const SourceMatch& a_match,
        const std::uint32_t a_selectedVirtualCopies
    ) {
        return std::cmp_greater(a_match.count, a_selectedVirtualCopies + 1);
    }

    [[nodiscard]] bool IsInVanillaRingSlot(
        RE::Actor& a_actor,
        const RE::TESObjectARMO& a_ring,
        const Core::ItemSource& a_source
    ) {
        return FindSourceMatch(a_actor, a_ring, a_source).rightWorn;
    }

    [[nodiscard]] std::optional<Core::ItemSource> ResolveEquippedSource(
        const Core::Assignment& a_assignment,
        const RE::FormID a_sourceFormID,
        const RE::ObjectEquipParams& a_params
    ) {
        if (!a_assignment.source.MatchesSourceFormID(a_sourceFormID)) {
            return std::nullopt;
        }

        if (a_assignment.source.kind == Core::ItemSourceKind::kCustomEnchantment) {
            const auto signature = a_assignment.source.customEnchantment;
            const auto uniqueID = a_assignment.source.extraUniqueID;
            if (!a_params.extraDataList
                || !Inventory::MatchesCustomSelection(a_params.extraDataList, signature, uniqueID)) {
                return std::nullopt;
            }

            return Core::ItemSource {
                .kind = Core::ItemSourceKind::kCustomEnchantment,
                .sourceFormID = a_sourceFormID,
                .customEnchantment = signature,
                .extraUniqueID = uniqueID,
            };
        }

        if (a_assignment.source.kind
            != Core::ItemSourceKind::kFormOnly
            || Inventory::HasCustomEnchantment(a_params.extraDataList)) {
            return std::nullopt;
        }

        return Core::ItemSource {
            .kind = Core::ItemSourceKind::kFormOnly,
            .sourceFormID = a_sourceFormID,
        };
    }

    [[nodiscard]] std::optional<VirtualSourceTarget> FindVirtualTargetForVanillaRingSlotEquip(
        const Core::ActorKey a_actorKey,
        RE::Actor& a_actor,
        const RE::TESObjectARMO& a_ring,
        const RE::ObjectEquipParams& a_params
    ) {
        const auto sourceFormID = a_ring.GetFormID();
        const auto snapshot = AssignmentStore::GetSnapshot(a_actorKey);
        for (const auto target : Core::kVirtualTargets) {
            const auto& selection = snapshot.byTarget[Core::ToIndex(target)];
            const auto source = ResolveEquippedSource(selection, sourceFormID, a_params);
            if (!source) {
                continue;
            }

            const auto sourceMatches = FindSourceMatch(a_actor, a_ring, *source);
            const auto selectedCopies = CountSelectedVirtualCopies(a_actorKey, *source, target);
            if (HasSpareCopyAfterVanillaEquip(sourceMatches, selectedCopies)) {
                continue;
            }

            return VirtualSourceTarget {
                .target = target,
                .source = *source,
            };
        }

        return std::nullopt;
    }

    [[nodiscard]] std::optional<Core::Target> FindVirtualTargetForVanillaRingSlotEquip(
        const Core::ActorKey a_actorKey,
        RE::Actor& a_actor,
        const RE::TESObjectARMO& a_ring,
        const Core::ItemSource& a_source
    ) {
        const auto snapshot = AssignmentStore::GetSnapshot(a_actorKey);
        for (const auto target : Core::kVirtualTargets) {
            const auto& selection = snapshot.byTarget[Core::ToIndex(target)];
            if (!selection.source.Matches(a_source)) {
                continue;
            }

            const auto sourceMatches = FindSourceMatch(a_actor, a_ring, a_source);
            const auto selectedCopies = CountSelectedVirtualCopies(a_actorKey, a_source, target);
            if (HasSpareCopyAfterVanillaEquip(sourceMatches, selectedCopies)) {
                continue;
            }

            return target;
        }

        return std::nullopt;
    }

    [[nodiscard]] RE::FormID EquipSlotFormID(const RE::ObjectEquipParams& a_params) {
        return a_params.equipSlot ? a_params.equipSlot->GetFormID() : RE::FormID {0};
    }

    [[nodiscard]] ActionResult MoveVirtualToVanillaRingSlot(
        const Core::ActorKey a_actor,
        const Core::ItemSource& a_source,
        const RE::FormID a_equipSlotFormID,
        const bool a_forceEquip,
        const Core::Target a_target
    ) {
        ActionResult result;
        auto* actor = Core::ResolveActor(a_actor);
        auto* ring = LookupSourceRing(a_source.sourceFormID);
        if (!actor || !ring) {
            return result;
        }

        RE::ExtraDataList* equipExtraList = nullptr;
        if (a_source.IsCustomEnchantment()) {
            const auto sourceMatches = FindSourceMatch(*actor, *ring, a_source);
            equipExtraList = sourceMatches.equipExtraList;
            if (!equipExtraList) {
                result.selectionChanged = ClearVirtualAssignment(*actor, a_target);
                return result;
            }
        }

        const auto selection = AssignmentStore::Get(a_actor, a_target);
        if (selection.source.Matches(a_source)) {
            AssignmentStore::Clear(a_actor, a_target);
            VirtualSlots::ClearTarget(a_actor, a_target);
            result.selectionChanged = true;
        }

        auto* equipManager = RE::ActorEquipManager::GetSingleton();
        if (!equipManager) {
            return result;
        }

        const auto* equipSlot = a_equipSlotFormID == 0 ? nullptr
                                                       : RE::TESForm::LookupByID<RE::BGSEquipSlot>(a_equipSlotFormID);
        if (a_equipSlotFormID != 0 && !equipSlot) {
            return result;
        }

        equipManager->EquipObject(actor, ring, equipExtraList, 1, equipSlot, true, a_forceEquip, false, true);
        if (IsInVanillaRingSlot(*actor, *ring, a_source)) {
            Audio::EquipSounds::Play(*actor, *ring, Audio::EquipSounds::Cue::kEquip);
        }
        result.inventoryChanged = true;
        return result;
    }

    [[nodiscard]] bool UnequipVanillaRingSlot(
        RE::Actor& a_actor,
        RE::TESObjectARMO& a_ring,
        RE::ExtraDataList* a_extraList
    ) {
        if (Inventory::IsProtectedRingStack(a_extraList)) {
            return false;
        }

        auto* equipManager = RE::ActorEquipManager::GetSingleton();
        if (!equipManager) {
            return false;
        }

        equipManager->UnequipObject(
            std::addressof(a_actor),
            std::addressof(a_ring),
            a_extraList,
            1,
            nullptr,
            true,
            false,
            false,
            true,
            nullptr
        );

        return true;
    }

    [[nodiscard]] bool EquipVanillaRingSlot(
        RE::Actor& a_actor,
        RE::TESObjectARMO& a_ring,
        RE::ExtraDataList* a_extraList
    ) {
        auto* equipManager = RE::ActorEquipManager::GetSingleton();
        if (!equipManager) {
            return false;
        }

        auto* equipSlot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(kRightHandEquipSlotFormID);
        equipManager->EquipObject(
            std::addressof(a_actor),
            std::addressof(a_ring),
            a_extraList,
            1,
            equipSlot,
            true,
            false,
            false,
            true
        );

        return true;
    }

    [[nodiscard]] bool AssignSourceToTarget(
        const Core::ActorKey a_actor,
        RE::TESObjectARMO& a_ring,
        const Core::ItemSource& a_source,
        const Core::Target a_target,
        const std::optional<Core::Target> a_moveSourceTarget = std::nullopt
    ) {
        if (a_source.IsCustomEnchantment()) {
            return AssignmentStore::AssignCustom(
                a_actor,
                a_ring,
                a_source.customEnchantment,
                a_source.extraUniqueID,
                a_target,
                a_moveSourceTarget
            );
        }

        return AssignmentStore::AssignForm(a_actor, a_ring, a_target, a_moveSourceTarget);
    }

    void MergeActionResult(ActionResult& a_result, const ActionResult a_next) {
        a_result.selectionChanged = a_result.selectionChanged || a_next.selectionChanged;
        a_result.inventoryChanged = a_result.inventoryChanged || a_next.inventoryChanged;
        a_result.sourceUnavailable = a_result.sourceUnavailable || a_next.sourceUnavailable;
        a_result.handled = a_result.handled || a_next.handled;
        if (a_result.blockReason == ActionBlockReason::kNone) {
            a_result.blockReason = a_next.blockReason;
        }
    }

    [[nodiscard]] ActionResult PrepareVanillaRingSlotForVirtualTarget(
        RE::Actor& a_actor,
        const Core::TargetMask& a_occupiedTargets
    ) {
        ActionResult result;
        if (!a_occupiedTargets.Contains(Core::kVanillaRingSlotTarget)) {
            return result;
        }

        const auto rightWorn = Inventory::FindRightWornRing(a_actor);
        if (!rightWorn) {
            return result;
        }

        if (!rightWorn->ring || rightWorn->protectedStack) {
            return RightHandRingCannotBeUnequippedResult();
        }

        if (!UnequipVanillaRingSlot(a_actor, *rightWorn->ring, rightWorn->extraList)
            || Inventory::HasRightWornRing(a_actor)) {
            return RightHandRingCannotBeUnequippedResult();
        }

        result.inventoryChanged = true;
        return result;
    }

    void ClearVanillaRingSlotConflict(
        ActionResult& a_result,
        const Core::ActorKey a_actor,
        const Core::Target a_target
    ) {
        auto* actor = Core::ResolveActor(a_actor);
        if (!actor) {
            return;
        }

        if (ClearVirtualAssignment(*actor, a_target)) {
            a_result.selectionChanged = true;
        }
    }

    void ClearVanillaRingSlotConflicts(
        ActionResult& a_result,
        const Core::ActorKey a_actor,
        const RE::TESObjectARMO& a_ring,
        const std::optional<Core::Target> a_selectedTarget
    ) {
        const auto occupiedTargets = SourceModelFootprints::GetProjectedTargets(a_ring, Core::kVanillaRingSlotTarget);
        const auto snapshot = AssignmentStore::GetSnapshot(a_actor);
        std::vector<Core::Target> conflicts;
        for (const auto target : Core::kVirtualTargets) {
            const auto& assignment = snapshot.byTarget[Core::ToIndex(target)];
            if (!assignment.IsAssigned()) {
                continue;
            }

            auto* assignedRing = LookupSourceRing(assignment.source.sourceFormID);
            if (!assignedRing) {
                continue;
            }

            if (occupiedTargets.Intersects(SourceModelFootprints::GetProjectedTargets(*assignedRing, target))) {
                conflicts.push_back(target);
            }
        }

        if (a_selectedTarget && std::ranges::find(conflicts, *a_selectedTarget) == conflicts.end()) {
            conflicts.push_back(*a_selectedTarget);
        }

        for (const auto target : conflicts) {
            ClearVanillaRingSlotConflict(a_result, a_actor, target);
        }
    }

    [[nodiscard]] ActionResult ToggleVanillaRingSlot(const Core::ActorKey a_actor, const Core::ItemSource& a_source) {
        ActionResult result;
        auto* actor = Core::ResolveActor(a_actor);
        auto* ring = LookupSourceRing(a_source.sourceFormID);
        if (!actor || !ring) {
            return result;
        }

        const auto sourceMatches = FindSourceMatch(*actor, *ring, a_source);
        if (!sourceMatches.HasMatch()) {
            return result;
        }

        if (sourceMatches.rightWorn) {
            if (sourceMatches.rightWornProtected) {
                return RightHandRingCannotBeUnequippedResult();
            }

            if (!UnequipVanillaRingSlot(*actor, *ring, sourceMatches.rightWornExtraList)) {
                return result;
            }

            result.inventoryChanged = true;
            if (!IsInVanillaRingSlot(*actor, *ring, a_source)) {
                Audio::EquipSounds::Play(*actor, *ring, Audio::EquipSounds::Cue::kUnequip);
            }
            return result;
        }

        const auto target = FindVirtualTargetForVanillaRingSlotEquip(a_actor, *actor, *ring, a_source);

        if (!EquipVanillaRingSlot(*actor, *ring, sourceMatches.equipExtraList)) {
            return result;
        }

        ClearVanillaRingSlotConflicts(result, a_actor, *ring, target);
        result.inventoryChanged = true;
        if (IsInVanillaRingSlot(*actor, *ring, a_source)) {
            Audio::EquipSounds::Play(*actor, *ring, Audio::EquipSounds::Cue::kEquip);
        }
        return result;
    }

    void QueueMoveVirtualToVanillaRingSlot(
        const Core::ActorKey a_actor,
        Core::ItemSource a_source,
        const RE::ObjectEquipParams& a_params,
        const Core::Target a_target,
        CompletionCallback a_onComplete
    ) {
        stl::add_task([a_actor,
                          source = std::move(a_source),
                          equipSlotFormID = EquipSlotFormID(a_params),
                          forceEquip = a_params.forceEquip,
                          a_target,
                          onComplete = std::move(a_onComplete)] {
            const auto result = MoveVirtualToVanillaRingSlot(a_actor, source, equipSlotFormID, forceEquip, a_target);
            if (onComplete) {
                onComplete(result);
            }
        });
    }

    [[nodiscard]] bool EnforceTargetInvariant(RE::Actor& a_actor, const Core::Target a_target) {
        const auto actorKey = Core::MakeActorKey(a_actor);
        const auto selection = AssignmentStore::Get(actorKey, a_target);
        const auto source = GetItemSource(selection);
        if (!source) {
            return false;
        }

        auto* ring = LookupSourceRing(source->sourceFormID);
        if (!ring) {
            return false;
        }

        const auto occupiedTargets = SourceModelFootprints::GetProjectedTargets(*ring, a_target);
        if (occupiedTargets.Empty()) {
            return ClearVirtualAssignment(a_actor, a_target);
        }

        if (!Settings::GetSingleton()->AreTargetsEnabled(occupiedTargets)) {
            return ClearVirtualAssignment(a_actor, a_target);
        }

        if (occupiedTargets.Contains(Core::kVanillaRingSlotTarget) && Inventory::HasRightWornRing(a_actor)) {
            return ClearVirtualAssignment(a_actor, a_target);
        }

        const auto sourceMatches = FindSourceMatch(a_actor, *ring, *source);
        const auto selectedCopies = CountSelectedVirtualCopies(actorKey, *source);
        const auto vanillaSlotClaim = sourceMatches.rightWorn ? 1u : 0u;
        const auto requiredInventoryCount = selectedCopies + vanillaSlotClaim;
        const auto shouldClear = !sourceMatches.HasMatch()
                                 || std::cmp_less(sourceMatches.count, requiredInventoryCount);
        if (shouldClear) {
            return ClearVirtualAssignment(a_actor, a_target);
        }
        return false;
    }

    [[nodiscard]] ActionResult RightHandRingCannotBeUnequippedResult() {
        return ActionResult {
            .handled = true,
            .blockReason = ActionBlockReason::kRightHandRingCannotBeUnequipped,
        };
    }

}

bool IsSelected(const SourceSelection& a_selection, const Core::Target a_target) {
    if (!Core::IsVirtualTarget(a_target) || !a_selection.itemSource.IsAssigned()) {
        return false;
    }

    return AssignmentStore::Get(a_selection.actor, a_target).source.Matches(a_selection.itemSource);
}

bool IsInVanillaRingSlot(const SourceSelection& a_selection) {
    auto* actor = Core::ResolveActor(a_selection.actor);
    auto* ring = LookupSourceRing(a_selection.itemSource.sourceFormID);
    if (!actor || !ring) {
        return false;
    }

    return IsInVanillaRingSlot(*actor, *ring, a_selection.itemSource);
}

bool IsProtectedInVanillaRingSlot(const SourceSelection& a_selection) {
    auto* actor = Core::ResolveActor(a_selection.actor);
    auto* ring = LookupSourceRing(a_selection.itemSource.sourceFormID);
    if (!actor || !ring) {
        return false;
    }

    const auto sourceMatches = FindSourceMatch(*actor, *ring, a_selection.itemSource);
    return sourceMatches.rightWorn && sourceMatches.rightWornProtected;
}

std::vector<Core::Target> CollectSelectedTargetsOnHand(const SourceSelection& a_selection, const Core::Hand a_hand) {
    std::vector<Core::Target> targets;
    targets.reserve(Core::kAllTargets.size());

    for (const auto target : Core::kVirtualTargets) {
        if (target.hand == a_hand && IsSelected(a_selection, target)) {
            targets.push_back(target);
        }
    }

    if (a_hand == Core::Hand::kRight && IsInVanillaRingSlot(a_selection)) {
        targets.push_back(Core::kVanillaRingSlotTarget);
    }

    return targets;
}

namespace {
    [[nodiscard]] ActionResult MoveVanillaRingSlotToVirtual(
        const Core::ActorKey a_actor,
        const Core::ItemSource& a_source,
        const Core::Target a_target
    ) {
        ActionResult result;
        if (!CanUseVirtualTarget(a_target, "moveVanillaRingSlotToVirtual"sv)) {
            return result;
        }

        auto* actor = Core::ResolveActor(a_actor);
        auto* ring = LookupSourceRing(a_source.sourceFormID);
        if (!actor || !ring) {
            return result;
        }

        auto sourceMatches = FindSourceMatch(*actor, *ring, a_source);
        if (!sourceMatches.HasMatch()) {
            if (!a_source.IsCustomEnchantment()
                && AssignmentStore::Get(a_actor, a_target).source.sourceFormID
                == a_source.sourceFormID) {
                result.selectionChanged = ClearVirtualAssignment(*actor, a_target);
            }
            return result;
        }

        const auto occupiedTargets = SourceModelFootprints::GetProjectedTargets(*ring, a_target);
        if (!CanUseEnabledTargets(occupiedTargets, a_target, *ring, "moveVanillaRingSlotToVirtual"sv)) {
            return result;
        }

        const auto selectedCopies = CountSelectedVirtualCopies(a_actor, a_source, a_target);
        if (RightSlotConsumesNeededCopy(sourceMatches, selectedCopies)) {
            if (sourceMatches.rightWornProtected) {
                return RightHandRingCannotBeUnequippedResult();
            }

            if (!UnequipVanillaRingSlot(*actor, *ring, sourceMatches.rightWornExtraList)) {
                return result;
            }

            result.inventoryChanged = true;
            sourceMatches = FindSourceMatch(*actor, *ring, a_source);
            if ((a_source.IsCustomEnchantment()
                    && (!sourceMatches.HasMatch() || RightSlotConsumesNeededCopy(sourceMatches, selectedCopies)))
                || (!a_source.IsCustomEnchantment() && sourceMatches.rightWorn)) {
                return result;
            }
        }

        const auto prepareResult = PrepareVanillaRingSlotForVirtualTarget(*actor, occupiedTargets);
        if (prepareResult.blockReason != ActionBlockReason::kNone) {
            return prepareResult;
        }
        MergeActionResult(result, prepareResult);

        if (!AssignSourceToTarget(a_actor, *ring, a_source, a_target)) {
            return result;
        }

        VirtualSlots::RequestRefresh(
            a_actor,
            VirtualSlots::RefreshOptions {
                .soundTarget = a_target,
                .sound = Audio::EquipSounds::Cue::kEquip,
            }
        );

        result.selectionChanged = true;
        return result;
    }

    void QueueVanillaRingSlotToVirtual(
        const Core::ActorKey a_actor,
        Core::ItemSource a_source,
        const Core::Target a_target,
        CompletionCallback a_onComplete
    ) {
        if (!CanUseVirtualTarget(a_target, "queueVanillaRingSlotToVirtual"sv)) {
            return;
        }

        stl::add_task([a_actor, source = std::move(a_source), a_target, onComplete = std::move(a_onComplete)] {
            const auto result = MoveVanillaRingSlotToVirtual(a_actor, source, a_target);
            if (onComplete) {
                onComplete(result);
            }
        });
    }

    [[nodiscard]] ActionResult ToggleVanillaTarget(const SourceSelection& a_selection) {
        if (auto* actor = Core::ResolveActor(a_selection.actor);
            actor && Inventory::HasProtectedRightWornRing(*actor)) {
            return RightHandRingCannotBeUnequippedResult();
        }

        return ToggleVanillaRingSlot(a_selection.actor, a_selection.itemSource);
    }

    [[nodiscard]] ActionResult ClearSelectedVirtualTarget(
        const SourceSelection& a_selection,
        const Core::Target a_target
    ) {
        ActionResult result;
        AssignmentStore::Clear(a_selection.actor, a_target);
        VirtualSlots::RequestRefresh(
            a_selection.actor,
            VirtualSlots::RefreshOptions {
                .soundTarget = a_target,
                .sound = Audio::EquipSounds::Cue::kUnequip,
            }
        );
        result.selectionChanged = true;
        return result;
    }

    [[nodiscard]] ActionResult CompleteQueuedMove() {
        return ActionResult {
            .handled = true,
        };
    }

    [[nodiscard]] ActionResult MoveRightWornSourceToVirtual(
        const Core::ActorKey a_actor,
        Core::ItemSource a_source,
        const Core::Target a_target,
        const QueueMode a_queueMode,
        CompletionCallback a_onQueuedComplete
    ) {
        if (a_queueMode == QueueMode::kQueued) {
            QueueVanillaRingSlotToVirtual(a_actor, std::move(a_source), a_target, std::move(a_onQueuedComplete));
            return CompleteQueuedMove();
        }

        return MoveVanillaRingSlotToVirtual(a_actor, a_source, a_target);
    }

    [[nodiscard]] ActionResult AssignCustomTarget(
        const SourceSelection& a_selection,
        RE::TESObjectARMO& a_ring,
        const Core::Target a_target,
        const std::optional<Core::Target> a_moveSourceTarget,
        const QueueMode a_queueMode,
        CompletionCallback a_onQueuedComplete
    ) {
        ActionResult result;
        auto* actor = Core::ResolveActor(a_selection.actor);
        if (!actor || !a_selection.itemSource.IsCustomEnchantment()) {
            return result;
        }

        auto source = a_selection.itemSource;
        auto sourceMatches = FindSourceMatch(*actor, a_ring, source);
        auto* sourceExtraList = sourceMatches.equipExtraList;
        if (a_selection.preferredExtraList
            && Inventory::MatchesCustomSelection(
                a_selection.preferredExtraList,
                a_selection.itemSource.customEnchantment,
                a_selection.itemSource.extraUniqueID
            )) {
            sourceExtraList = a_selection.preferredExtraList;
        }
        if (!sourceExtraList || !sourceMatches.HasMatch()) {
            return result;
        }

        if (!Inventory::MatchesCustomSelection(
                sourceExtraList,
                a_selection.itemSource.customEnchantment,
                a_selection.itemSource.extraUniqueID
            )) {
            return result;
        }

        const auto selectedCopies = CountSelectedVirtualCopies(a_selection.actor, source, a_target);
        if (RightSlotConsumesNeededCopy(sourceMatches, selectedCopies)) {
            if (sourceMatches.rightWornProtected) {
                return RightHandRingCannotBeUnequippedResult();
            }

            return MoveRightWornSourceToVirtual(
                a_selection.actor,
                std::move(source),
                a_target,
                a_queueMode,
                std::move(a_onQueuedComplete)
            );
        }

        const auto hasNoFreeVirtualCopy = HasNoFreeVirtualCopy(sourceMatches, selectedCopies);
        const auto moveSourceTarget = hasNoFreeVirtualCopy ? a_moveSourceTarget : std::nullopt;
        if (hasNoFreeVirtualCopy && !moveSourceTarget) {
            return result;
        }

        const auto occupiedTargets = SourceModelFootprints::GetProjectedTargets(a_ring, a_target);
        if (!CanUseEnabledTargets(occupiedTargets, a_target, a_ring, "assignCustomTarget"sv)) {
            return result;
        }

        const auto prepareResult = PrepareVanillaRingSlotForVirtualTarget(*actor, occupiedTargets);
        if (prepareResult.blockReason != ActionBlockReason::kNone) {
            return prepareResult;
        }
        MergeActionResult(result, prepareResult);

        if (!AssignSourceToTarget(a_selection.actor, a_ring, source, a_target, moveSourceTarget)) {
            return result;
        }

        VirtualSlots::RequestRefresh(
            a_selection.actor,
            VirtualSlots::RefreshOptions {
                .soundTarget = a_target,
                .sound = Audio::EquipSounds::Cue::kEquip,
            }
        );
        result.selectionChanged = true;
        return result;
    }

    [[nodiscard]] ActionResult AssignFormTarget(
        const SourceSelection& a_selection,
        RE::TESObjectARMO& a_ring,
        const Core::Target a_target,
        const std::optional<Core::Target> a_moveSourceTarget,
        const QueueMode a_queueMode,
        CompletionCallback a_onQueuedComplete
    ) {
        ActionResult result;
        auto* actor = Core::ResolveActor(a_selection.actor);
        if (!actor) {
            return result;
        }

        auto source = a_selection.itemSource;
        auto sourceMatches = FindSourceMatch(*actor, a_ring, source);
        if (!sourceMatches.HasMatch()) {
            result.sourceUnavailable = true;
            result.handled = true;
            return result;
        }

        const auto selectedCopies = CountSelectedVirtualCopies(a_selection.actor, source, a_target);
        if (RightSlotConsumesNeededCopy(sourceMatches, selectedCopies)) {
            if (sourceMatches.rightWornProtected) {
                return RightHandRingCannotBeUnequippedResult();
            }

            return MoveRightWornSourceToVirtual(
                a_selection.actor,
                std::move(source),
                a_target,
                a_queueMode,
                std::move(a_onQueuedComplete)
            );
        }

        const auto hasNoFreeVirtualCopy = HasNoFreeVirtualCopy(sourceMatches, selectedCopies);
        const auto moveSourceTarget = hasNoFreeVirtualCopy ? a_moveSourceTarget : std::nullopt;
        if (hasNoFreeVirtualCopy && !moveSourceTarget) {
            return result;
        }

        const auto occupiedTargets = SourceModelFootprints::GetProjectedTargets(a_ring, a_target);
        if (!CanUseEnabledTargets(occupiedTargets, a_target, a_ring, "assignFormTarget"sv)) {
            return result;
        }

        const auto prepareResult = PrepareVanillaRingSlotForVirtualTarget(*actor, occupiedTargets);
        if (prepareResult.blockReason != ActionBlockReason::kNone) {
            return prepareResult;
        }
        MergeActionResult(result, prepareResult);

        if (!AssignSourceToTarget(a_selection.actor, a_ring, source, a_target, moveSourceTarget)) {
            return result;
        }

        VirtualSlots::RequestRefresh(
            a_selection.actor,
            VirtualSlots::RefreshOptions {
                .soundTarget = a_target,
                .sound = Audio::EquipSounds::Cue::kEquip,
            }
        );
        result.selectionChanged = true;
        return result;
    }
}

ActionResult ToggleTarget(
    const SourceSelection& a_selection,
    const Core::Target a_target,
    const std::optional<Core::Target> a_moveSourceTarget,
    const QueueMode a_queueMode,
    CompletionCallback a_onQueuedComplete
) {
    ActionResult result;
    if (a_target == Core::kVanillaRingSlotTarget) {
        return ToggleVanillaTarget(a_selection);
    }

    if (!CanUseVirtualTarget(a_target, "toggleTarget"sv)) {
        return result;
    }

    auto* ring = LookupSourceRing(a_selection.itemSource.sourceFormID);
    if (!ring) {
        return result;
    }

    if (IsSelected(a_selection, a_target)) {
        return ClearSelectedVirtualTarget(a_selection, a_target);
    }

    if (a_selection.itemSource.IsCustomEnchantment()) {
        return AssignCustomTarget(
            a_selection,
            *ring,
            a_target,
            a_moveSourceTarget,
            a_queueMode,
            std::move(a_onQueuedComplete)
        );
    }

    return AssignFormTarget(
        a_selection,
        *ring,
        a_target,
        a_moveSourceTarget,
        a_queueMode,
        std::move(a_onQueuedComplete)
    );
}

ActionResult ClearDisabledVirtualSlotAssignments(const RefreshMode a_refreshMode) {
    ActionResult result;
    for (const auto& actorSnapshot : AssignmentStore::GetAllSnapshots()) {
        auto actorChanged = false;
        for (const auto target : Core::kVirtualTargets) {
            const auto& assignment = actorSnapshot.assignments.byTarget[Core::ToIndex(target)];
            if (!assignment.IsAssigned()) {
                continue;
            }

            auto occupiedTargets = Core::TargetMask {};
            if (auto* ring = LookupSourceRing(assignment.source.sourceFormID)) {
                occupiedTargets = SourceModelFootprints::GetProjectedTargets(*ring, target);
            }
            if (occupiedTargets.Empty()) {
                occupiedTargets.Add(target);
            }

            if (Settings::GetSingleton()->AreTargetsEnabled(occupiedTargets)) {
                continue;
            }

            AssignmentStore::Clear(actorSnapshot.actor, target);
            actorChanged = true;
            result.selectionChanged = true;
        }

        if (actorChanged && a_refreshMode == RefreshMode::kAffectedActors) {
            VirtualSlots::RequestRefresh(actorSnapshot.actor);
        }
    }

    return result;
}

bool InterceptRightEquip(
    RE::Actor& a_actor,
    const RE::TESObjectARMO& a_ring,
    const RE::ObjectEquipParams& a_params,
    CompletionCallback a_onQueuedComplete
) {
    if (a_params.equipSlot && a_params.equipSlot->GetFormID() == kLeftHandEquipSlotFormID) {
        return false;
    }

    const auto actorKey = Core::MakeActorKey(a_actor);
    const auto selectedSource = FindVirtualTargetForVanillaRingSlotEquip(actorKey, a_actor, a_ring, a_params);
    if (!selectedSource) {
        return false;
    }

    QueueMoveVirtualToVanillaRingSlot(
        actorKey,
        selectedSource->source,
        a_params,
        selectedSource->target,
        std::move(a_onQueuedComplete)
    );
    return true;
}

void QueueAssignmentReconciliation(const Core::ActorKey a_actor, CompletionCallback a_onComplete) {
    stl::add_task([a_actor, onComplete = std::move(a_onComplete)] {
        auto* actor = Core::ResolveActor(a_actor);
        if (!actor) {
            return;
        }

        ActionResult result;
        for (const auto target : Core::kVirtualTargets) {
            result.selectionChanged = EnforceTargetInvariant(*actor, target) || result.selectionChanged;
        }

        VirtualSlots::RequestRefresh(a_actor);
        if (onComplete && result.selectionChanged) {
            onComplete(result);
        }
    });
}

void HandleContainerChangedForAssignments(
    const Core::ActorKey a_actor,
    const RE::TESContainerChangedEvent& a_event,
    CompletionCallback a_onComplete
) {
    if (!AssignmentStore::ContainsSource(a_actor, a_event.baseObj)) {
        return;
    }

    auto* actor = Core::ResolveActor(a_actor);
    if (!actor) {
        return;
    }

    const auto actorFormID = actor->GetFormID();
    if (a_event.oldContainer != actorFormID && a_event.newContainer != actorFormID) {
        return;
    }

    QueueAssignmentReconciliation(a_actor, std::move(a_onComplete));
}
}
