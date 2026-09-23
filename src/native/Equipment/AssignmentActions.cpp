#include "Equipment/AssignmentActions.h"

#include <RE/Skyrim.h> // IWYU pragma: keep
#include <SKSE/SKSE.h> // IWYU pragma: keep

#include "Audio/EquipSounds.h"
#include "Core/ActorKey.h"
#include "Core/ItemSource.h"
#include "Core/Target.h"
#include "Core/TargetMask.h"
#include "Equipment/AssignmentStore.h"
#include "Equipment/RaceSwitchRestore.h"
#include "Equipment/SavedEquipment.h"
#include "Equipment/SpecialRingRules.h"
#include "Inventory.h"
#include "Papyrus/ScriptEventMirror.h"
#include "Settings.h"
#include "SourceModelFootprints.h"
#include "VirtualSlots.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

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

        SKSE::log::warn(
            "Equipment: virtual target rejected | action={} | target={}",
            a_action,
            Core::TargetName(a_target)
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

    [[nodiscard]] ActionResult RightHandRingCannotBeUnequippedResult();
    [[nodiscard]] ActionResult UnequipRightWornRingForReplacement(RE::Actor& a_actor);
    void MergeActionResult(ActionResult& a_result, ActionResult a_next);
    void ClearVanillaRingSlotConflicts(
        ActionResult& a_result,
        Core::ActorKey a_actor,
        const RE::TESObjectARMO& a_ring,
        std::optional<Core::Target> a_selectedTarget
    );

    [[nodiscard]] bool ClearVirtualAssignment(
        const Core::ActorKey a_actor,
        const Core::Target a_target,
        const VirtualSlots::ScriptBindingClearMode a_scriptBindings = VirtualSlots::ScriptBindingClearMode::kRelease
    ) {
        const auto hadAssignment = AssignmentStore::Get(a_actor, a_target).IsAssigned();
        AssignmentStore::Clear(a_actor, a_target);
        VirtualSlots::ClearTarget(a_actor, a_target, Audio::EquipSounds::Cue::kNone, a_scriptBindings);
        return hadAssignment;
    }

    [[nodiscard]] bool ClearVirtualAssignment(
        RE::Actor const& a_actor,
        const Core::Target a_target,
        const VirtualSlots::ScriptBindingClearMode a_scriptBindings = VirtualSlots::ScriptBindingClearMode::kRelease
    ) {
        return ClearVirtualAssignment(Core::MakeActorKey(a_actor), a_target, a_scriptBindings);
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

        const auto sourceMatches = Inventory::FindSourceMatches(a_actor, a_source);
        return SourceMatch {
            .equipExtraList = sourceMatches.firstExtraList,
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
        const auto rightWorn = Inventory::FindRightWornRing(a_actor);
        return rightWorn && Inventory::RightWornRingMatchesSource(*rightWorn, a_ring, a_source);
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
            || !Inventory::MatchesSource(a_params.extraDataList, a_assignment.source)) {
            return std::nullopt;
        }

        return a_assignment.source;
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
            if (!a_source.Matches(selection.source)) {
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

        const auto sourceMatches = FindSourceMatch(*actor, *ring, a_source);
        auto* equipExtraList = sourceMatches.equipExtraList;
        if (!sourceMatches.HasMatch() || (a_source.IsCustomEnchantment() && !equipExtraList)) {
            result.selectionChanged = ClearVirtualAssignment(*actor, a_target);
            return result;
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

        const auto occupiedTargets = SourceModelFootprints::GetProjectedRingGeometryTargets(
            *ring,
            Core::kVanillaRingSlotTarget
        );
        if (occupiedTargets.Empty()) {
            return result;
        }

        const auto clearResult = UnequipRightWornRingForReplacement(*actor);
        if (clearResult.blockReason != ActionBlockReason::kNone) {
            return clearResult;
        }
        MergeActionResult(result, clearResult);

        equipExtraList = FindSourceMatch(*actor, *ring, a_source).equipExtraList;
        if (a_source.extraUniqueID && !equipExtraList) {
            result.sourceUnavailable = true;
            return result;
        }
        const auto selection = AssignmentStore::Get(a_actor, a_target);
        if (a_source.Matches(selection.source)) {
            AssignmentStore::Clear(a_actor, a_target);
            VirtualSlots::ClearTarget(a_actor, a_target);
            result.selectionChanged = true;
        }

        equipManager->EquipObject(actor, ring, equipExtraList, 1, equipSlot, true, a_forceEquip, false, true);
        ClearVanillaRingSlotConflicts(result, a_actor, *ring, std::nullopt);
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
        if (Inventory::IsUnequipProtectedRingStack(a_extraList)) {
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

        auto const* equipSlot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(kRightHandEquipSlotFormID);
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
        RE::TESObjectARMO const& a_ring,
        const Core::ItemSource& a_source,
        const Core::Target a_target,
        const std::optional<Core::Target> a_moveSourceTarget = std::nullopt
    ) {
        const auto assigned = AssignmentStore::Assign(a_actor, a_ring, a_source, a_target, a_moveSourceTarget);
        if (assigned) {
            RaceSwitchRestore::DiscardReplacedTargets(
                a_actor,
                SourceModelFootprints::GetProjectedRingGeometryTargets(a_ring, a_target)
            );
        }
        return assigned;
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

    [[nodiscard]] ActionResult UnequipRightWornRingForReplacement(RE::Actor& a_actor) {
        ActionResult result;
        switch (Inventory::UnequipRightWornRing(a_actor)) {
            case Inventory::RightWornRingUnequipResult::kNone:       break;
            case Inventory::RightWornRingUnequipResult::kUnequipped: result.inventoryChanged = true; break;
            case Inventory::RightWornRingUnequipResult::kProtected:
            case Inventory::RightWornRingUnequipResult::kFailed:     return RightHandRingCannotBeUnequippedResult();
        }

        return result;
    }

    [[nodiscard]] bool ConflictsWithRightWornRing(RE::Actor& a_actor, const Core::TargetMask& a_targets) {
        if (a_targets.Empty()) {
            return false;
        }

        const auto rightWorn = Inventory::FindRightWornRing(a_actor);
        if (!rightWorn || !rightWorn->ring) {
            return false;
        }

        const auto rightWornTargets = SourceModelFootprints::GetProjectedRingGeometryTargets(
            *rightWorn->ring,
            Core::kVanillaRingSlotTarget
        );
        return !rightWornTargets.Empty() && rightWornTargets.Intersects(a_targets);
    }

    [[nodiscard]] ActionResult PrepareVanillaRingSlotForVirtualTarget(
        RE::Actor& a_actor,
        const Core::TargetMask& a_occupiedTargets
    ) {
        if (!ConflictsWithRightWornRing(a_actor, a_occupiedTargets)) {
            return {};
        }

        return UnequipRightWornRingForReplacement(a_actor);
    }

    void ClearVanillaRingSlotConflict(
        ActionResult& a_result,
        const Core::ActorKey a_actor,
        const Core::Target a_target
    ) {
        auto const* actor = Core::ResolveActor(a_actor);
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
        const auto occupiedTargets = SourceModelFootprints::GetProjectedRingGeometryTargets(
            a_ring,
            Core::kVanillaRingSlotTarget
        );
        const auto snapshot = AssignmentStore::GetSnapshot(a_actor);
        std::vector<Core::Target> conflicts;
        for (const auto target : Core::kVirtualTargets) {
            const auto& assignment = snapshot.byTarget[Core::ToIndex(target)];
            if (!assignment.IsAssigned()) {
                continue;
            }

            auto const* assignedRing = LookupSourceRing(assignment.source.sourceFormID);
            if (!assignedRing) {
                continue;
            }

            if (occupiedTargets.Intersects(
                    SourceModelFootprints::GetProjectedRingGeometryTargets(*assignedRing, target)
                )) {
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
        const auto occupiedTargets = SourceModelFootprints::GetProjectedRingGeometryTargets(
            *ring,
            Core::kVanillaRingSlotTarget
        );
        if (occupiedTargets.Empty()) {
            return result;
        }

        const auto clearResult = UnequipRightWornRingForReplacement(*actor);
        if (clearResult.blockReason != ActionBlockReason::kNone) {
            return clearResult;
        }
        MergeActionResult(result, clearResult);

        const auto currentSource = FindSourceMatch(*actor, *ring, a_source);
        if (!currentSource.HasMatch() || !EquipVanillaRingSlot(*actor, *ring, currentSource.equipExtraList)) {
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
        SKSE::GetTaskInterface()->AddTask([a_actor,
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

        auto const* ring = LookupSourceRing(source->sourceFormID);
        if (!ring) {
            return false;
        }

        const auto occupiedTargets = SourceModelFootprints::GetProjectedRingGeometryTargets(*ring, a_target);
        if (occupiedTargets.Empty()) {
            return ClearVirtualAssignment(a_actor, a_target);
        }

        if (!SpecialRingRules::AreTargetsEnabledForSource(actorKey, *ring, occupiedTargets)) {
            return ClearVirtualAssignment(a_actor, a_target);
        }

        if (ConflictsWithRightWornRing(a_actor, occupiedTargets)) {
            return ClearVirtualAssignment(a_actor, a_target);
        }

        const auto sourceMatches = FindSourceMatch(a_actor, *ring, *source);
        const auto selectedCopies = CountSelectedVirtualCopies(actorKey, *source);
        const auto shouldClear = !source->extraUniqueID
                                 || sourceMatches.count
                                 != 1
                                 || sourceMatches.rightWorn
                                 || selectedCopies
                                 != 1;
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

    const auto assigned = AssignmentStore::Get(a_selection.actor, a_target).source;
    if (a_selection.rowSources) {
        return std::ranges::any_of(*a_selection.rowSources, [&](const auto& a_source) {
            return a_source.IsSameCopy(assigned);
        });
    }
    return a_selection.itemSource.Matches(assigned);
}

bool IsInVanillaRingSlot(const SourceSelection& a_selection) {
    auto* actor = Core::ResolveActor(a_selection.actor);
    auto const* ring = LookupSourceRing(a_selection.itemSource.sourceFormID);
    if (!actor || !ring) {
        return false;
    }

    if (a_selection.rowSources) {
        return std::ranges::any_of(*a_selection.rowSources, [&](const auto& a_source) {
            return a_source.extraUniqueID && IsInVanillaRingSlot(*actor, *ring, a_source);
        });
    }
    return IsInVanillaRingSlot(*actor, *ring, a_selection.itemSource);
}

bool IsProtectedInVanillaRingSlot(const SourceSelection& a_selection) {
    auto* actor = Core::ResolveActor(a_selection.actor);
    auto const* ring = LookupSourceRing(a_selection.itemSource.sourceFormID);
    if (!actor || !ring) {
        return false;
    }

    if (a_selection.rowSources && !IsInVanillaRingSlot(a_selection)) {
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
        if (!CanUseVirtualTarget(a_target, std::string_view {"moveVanillaRingSlotToVirtual"})) {
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

        const auto occupiedTargets = SourceModelFootprints::GetProjectedRingGeometryTargets(*ring, a_target);
        if (!CanUseEnabledTargets(
                a_actor,
                occupiedTargets,
                a_target,
                *ring,
                std::string_view {"moveVanillaRingSlotToVirtual"}
            )) {
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

    [[nodiscard]] ActionResult AssignCustomTarget(
        const SourceSelection& a_selection,
        RE::TESObjectARMO const& a_ring,
        const Core::Target a_target,
        const std::optional<Core::Target> a_moveSourceTarget
    ) {
        ActionResult result;
        auto* actor = Core::ResolveActor(a_selection.actor);
        if (!actor || !a_selection.itemSource.IsCustomEnchantment()) {
            return result;
        }

        const auto& source = a_selection.itemSource;
        auto const sourceMatches = FindSourceMatch(*actor, a_ring, source);
        auto const* sourceExtraList = sourceMatches.equipExtraList;
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

            return MoveVanillaRingSlotToVirtual(a_selection.actor, source, a_target);
        }

        const auto hasNoFreeVirtualCopy = HasNoFreeVirtualCopy(sourceMatches, selectedCopies);
        const auto moveSourceTarget = hasNoFreeVirtualCopy ? a_moveSourceTarget : std::nullopt;
        if (hasNoFreeVirtualCopy && !moveSourceTarget) {
            return result;
        }

        const auto occupiedTargets = SourceModelFootprints::GetProjectedRingGeometryTargets(a_ring, a_target);
        if (!CanUseEnabledTargets(
                a_selection.actor,
                occupiedTargets,
                a_target,
                a_ring,
                std::string_view {"assignCustomTarget"}
            )) {
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
        RE::TESObjectARMO const& a_ring,
        const Core::Target a_target,
        const std::optional<Core::Target> a_moveSourceTarget
    ) {
        ActionResult result;
        auto* actor = Core::ResolveActor(a_selection.actor);
        if (!actor) {
            return result;
        }

        const auto& source = a_selection.itemSource;
        auto const sourceMatches = FindSourceMatch(*actor, a_ring, source);
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

            return MoveVanillaRingSlotToVirtual(a_selection.actor, source, a_target);
        }

        const auto hasNoFreeVirtualCopy = HasNoFreeVirtualCopy(sourceMatches, selectedCopies);
        const auto moveSourceTarget = hasNoFreeVirtualCopy ? a_moveSourceTarget : std::nullopt;
        if (hasNoFreeVirtualCopy && !moveSourceTarget) {
            return result;
        }

        const auto occupiedTargets = SourceModelFootprints::GetProjectedRingGeometryTargets(a_ring, a_target);
        if (!CanUseEnabledTargets(
                a_selection.actor,
                occupiedTargets,
                a_target,
                a_ring,
                std::string_view {"assignFormTarget"}
            )) {
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
    std::optional<Core::ItemSource> FindNativeRowCopy(
        RE::Actor& a_actor,
        const std::span<const Core::ItemSource> a_sources
    ) {
        for (const auto& source : a_sources) {
            if (source.extraUniqueID && Inventory::FindSourceMatches(a_actor, source).rightWorn) {
                return source;
            }
        }
        return std::nullopt;
    }

    std::optional<Core::ItemSource> ResolveRowCopy(
        RE::Actor& a_actor,
        const SourceSelection& a_selection,
        const std::span<const Core::ItemSource> a_sources,
        const Core::Target a_target,
        const std::optional<Core::Target> a_moveSourceTarget
    ) {
        const auto current = AssignmentStore::GetSnapshot(a_selection.actor);
        if (Core::IsVirtualTarget(a_target) && IsSelected(a_selection, a_target)) {
            return current.byTarget[Core::ToIndex(a_target)].source;
        }
        auto native = FindNativeRowCopy(a_actor, a_sources);
        if (a_target == Core::kVanillaRingSlotTarget && native) {
            return native;
        }
        std::vector<Core::ItemSource> claimed;
        for (const auto& assignment : current.byTarget) {
            if (assignment.IsAssigned()) {
                claimed.push_back(assignment.source);
            }
        }
        for (const auto& source : a_sources) {
            if (const auto copy = Inventory::AcquireCopy(a_actor, source, claimed, !source.extraUniqueID)) {
                return copy;
            }
        }
        if (a_moveSourceTarget && IsSelected(a_selection, *a_moveSourceTarget)) {
            return current.byTarget[Core::ToIndex(*a_moveSourceTarget)].source;
        }
        return native;
    }

    ActionResult ToggleRowTarget(
        const SourceSelection& a_selection,
        const std::span<const Core::ItemSource> a_sources,
        const Core::Target a_target,
        const std::optional<Core::Target> a_moveSourceTarget
    ) {
        auto* actor = Core::ResolveActor(a_selection.actor);
        auto const* ring = LookupSourceRing(a_selection.itemSource.sourceFormID);
        if (!actor || !ring) {
            return {};
        }
        const auto occupied = SourceModelFootprints::GetProjectedRingGeometryTargets(*ring, a_target);
        if (occupied.Empty()
            || (Core::IsVirtualTarget(a_target)
                && !SpecialRingRules::AreTargetsEnabledForSource(a_selection.actor, *ring, occupied))) {
            return {};
        }
        const auto selected = ResolveRowCopy(*actor, a_selection, a_sources, a_target, a_moveSourceTarget);
        if (!selected || !Inventory::FindSourceMatches(*actor, *selected).HasMatch()) {
            return {.sourceUnavailable = true, .handled = true};
        }
        auto result = ToggleTarget({.actor = a_selection.actor, .itemSource = *selected}, a_target, a_moveSourceTarget);
        result.inventoryChanged = result.inventoryChanged || result.selectionChanged;
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
    if (!Settings::GetSingleton()->IsActorVirtualRingSupportEnabled(a_selection.actor)) {
        return result;
    }
    if (a_selection.inventoryRevision
        && *a_selection.inventoryRevision
        != Inventory::SelectionRevision(a_selection.actor.referenceFormID)) {
        return {.sourceUnavailable = true, .handled = true};
    }
    if (a_queueMode == QueueMode::kQueued) {
        SKSE::GetTaskInterface()->AddTask(
            [selection = a_selection, a_target, a_moveSourceTarget, onComplete = std::move(a_onQueuedComplete)] {
                const auto completed = ToggleTarget(selection, a_target, a_moveSourceTarget);
                if (onComplete) {
                    onComplete(completed);
                }
            }
        );
        return {.handled = true};
    }

    if (a_selection.rowSources) {
        return ToggleRowTarget(a_selection, *a_selection.rowSources, a_target, a_moveSourceTarget);
    }

    if (a_target == Core::kVanillaRingSlotTarget) {
        return ToggleVanillaTarget(a_selection);
    }

    if (!CanUseVirtualTarget(a_target, std::string_view {"toggleTarget"})) {
        return result;
    }

    auto const* ring = LookupSourceRing(a_selection.itemSource.sourceFormID);
    if (!ring) {
        return result;
    }

    if (IsSelected(a_selection, a_target)) {
        return ClearSelectedVirtualTarget(a_selection, a_target);
    }

    if (a_selection.itemSource.IsCustomEnchantment()) {
        return AssignCustomTarget(a_selection, *ring, a_target, a_moveSourceTarget);
    }

    return AssignFormTarget(a_selection, *ring, a_target, a_moveSourceTarget);
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
            auto const* ring = LookupSourceRing(assignment.source.sourceFormID);
            if (ring) {
                occupiedTargets = SourceModelFootprints::GetProjectedRingGeometryTargets(*ring, target);
            }
            if (occupiedTargets.Empty()) {
                occupiedTargets.Add(target);
            }

            const auto targetsEnabled = ring ? SpecialRingRules::AreTargetsEnabledForSource(
                                                   actorSnapshot.actor,
                                                   *ring,
                                                   occupiedTargets
                                               )
                                             : Settings::GetSingleton()->AreTargetsEnabled(occupiedTargets);
            if (targetsEnabled) {
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

ActionResult ClearVirtualAssignments(
    RE::Actor const& a_actor,
    const VirtualSlots::ScriptBindingClearMode a_scriptBindings
) {
    ActionResult result;
    for (const auto target : Core::kVirtualTargets) {
        result.selectionChanged = ClearVirtualAssignment(a_actor, target, a_scriptBindings) || result.selectionChanged;
    }
    return result;
}

ActionResult ClearNonPlayerVirtualAssignments(const VirtualSlots::ScriptBindingClearMode a_scriptBindings) {
    ActionResult result;
    for (const auto& actorSnapshot : AssignmentStore::GetAllSnapshots()) {
        if (Core::IsPlayerActorKey(actorSnapshot.actor)) {
            continue;
        }

        for (const auto target : Core::kVirtualTargets) {
            if (!actorSnapshot.assignments.byTarget[Core::ToIndex(target)].IsAssigned()) {
                continue;
            }

            result.selectionChanged = ClearVirtualAssignment(actorSnapshot.actor, target, a_scriptBindings)
                                      || result.selectionChanged;
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
    SKSE::GetTaskInterface()->AddTask([a_actor, onComplete = std::move(a_onComplete)] {
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

namespace {
    std::optional<Core::ItemSource> AcquireLegacyCopy(
        RE::Actor& a_actor,
        const Core::Assignment& a_assignment,
        const std::vector<Core::ItemSource>& a_claimed,
        const bool a_bound
    ) {
        auto source = a_assignment.source;
        if (a_bound) {
            source.extraUniqueID = Papyrus::ScriptEventMirror::FindBoundCopyIdentity(
                a_actor,
                source,
                a_assignment.retainedEffectSourceFormID
            );
            return source.extraUniqueID ? Inventory::AcquireCopy(a_actor, source, a_claimed) : std::nullopt;
        }
        if (const auto copy = Inventory::AcquireCopy(a_actor, source, a_claimed)) {
            return copy;
        }
        source.extraUniqueID.reset();
        return Inventory::AcquireCopy(a_actor, source, a_claimed);
    }
}

void BindLegacyCopies(
    RE::Actor& a_actor,
    Core::TargetAssignments& a_snapshot,
    const Core::TargetAssignments& a_current
) {
    const auto bindings = Papyrus::ScriptEventMirror::GetBindingSnapshots();
    const auto actorKey = Core::MakeActorKey(a_actor);
    const auto hasBinding = [&](const Core::Target a_target) {
        const auto& assignment = a_snapshot.byTarget[Core::ToIndex(a_target)];
        return std::ranges::any_of(bindings, [&](const auto& a_binding) {
            return a_binding.actor
                   == actorKey
                   && a_binding.sourceFormID
                   == assignment.source.sourceFormID
                   && a_binding.effectSourceFormID
                   == assignment.retainedEffectSourceFormID;
        });
    };
    auto targets = Core::kVirtualTargets;
    // Unbound assignments must not take copies owned by serialized script bindings.
    std::ranges::stable_partition(targets, hasBinding);
    std::vector<Core::ItemSource> claimed;
    for (const auto& assignment : a_current.byTarget) {
        if (assignment.IsAssigned()) {
            claimed.push_back(assignment.source);
        }
    }
    for (const auto& assignment : a_snapshot.byTarget) {
        if (assignment.IsAssigned() && !assignment.needsCopyBinding) {
            claimed.push_back(assignment.source);
        }
    }
    for (const auto target : targets) {
        auto& assignment = a_snapshot.byTarget[Core::ToIndex(target)];
        if (!assignment.IsAssigned() || !assignment.needsCopyBinding) {
            continue;
        }
        if (a_current.byTarget[Core::ToIndex(target)].IsAssigned()) {
            assignment = {};
            continue;
        }
        const auto copy = AcquireLegacyCopy(a_actor, assignment, claimed, hasBinding(target));
        if (!copy) {
            assignment = {};
            continue;
        }
        assignment.source = *copy;
        assignment.needsCopyBinding = false;
        claimed.push_back(*copy);
    }
}

void HandleUniqueIDChange(const RE::TESUniqueIDChangeEvent& a_event) {
    if (a_event.oldUniqueID == 0 || a_event.newUniqueID == 0) {
        return;
    }
    const Core::ExtraUniqueIDKey oldID {.baseID = a_event.oldBaseID, .uniqueID = a_event.oldUniqueID};
    const Core::ExtraUniqueIDKey newID {.baseID = a_event.newBaseID, .uniqueID = a_event.newUniqueID};
    if (a_event.oldBaseID == a_event.newBaseID) {
        AssignmentStore::RemapUniqueID(oldID, newID);
        VirtualSlots::RemapUniqueID(oldID, newID);
    }
    SavedEquipment::RemapUniqueID(oldID, newID);
    RaceSwitchRestore::RemapUniqueID(oldID, newID);
}

void RestoreAvailableVirtualAssignments(RE::Actor& a_actor, const Core::TargetAssignments& a_snapshot) {
    const auto actorKey = Core::MakeActorKey(a_actor);
    auto restored = a_snapshot;
    BindLegacyCopies(a_actor, restored, AssignmentStore::GetSnapshot(actorKey));
    // Current selections take priority over saved equipment.
    for (const auto target : Core::kVirtualTargets) {
        const auto& assignment = restored.byTarget[Core::ToIndex(target)];
        auto const* ring = LookupSourceRing(assignment.source.sourceFormID);
        if (!assignment.IsAssigned() || !ring || AssignmentStore::Get(actorKey, target).IsAssigned()) {
            continue;
        }
        const auto occupied = SourceModelFootprints::GetProjectedRingGeometryTargets(*ring, target);
        const auto current = AssignmentStore::GetSnapshot(actorKey);
        const auto conflicts = std::ranges::any_of(Core::kVirtualTargets, [&](const Core::Target a_other) {
            const auto& selected = current.byTarget[Core::ToIndex(a_other)];
            auto const* selectedRing = LookupSourceRing(selected.source.sourceFormID);
            return selectedRing
                   && occupied.Intersects(
                       SourceModelFootprints::GetProjectedRingGeometryTargets(*selectedRing, a_other)
                   );
        });
        const auto match = FindSourceMatch(a_actor, *ring, assignment.source);
        const auto claimed = CountSelectedVirtualCopies(actorKey, assignment.source) + (match.rightWorn ? 1U : 0U);
        if (conflicts || ConflictsWithRightWornRing(a_actor, occupied) || std::cmp_less_equal(match.count, claimed)) {
            continue;
        }
        if (AssignSourceToTarget(actorKey, *ring, assignment.source, target)) {
            static_cast<void>(AssignmentStore::TrySetRetainedEffectSourceFormID(
                actorKey,
                target,
                assignment,
                assignment.retainedEffectSourceFormID
            ));
        }
    }
}

void HandleContainerChangedForAssignments(
    const Core::ActorKey a_actor,
    const RE::TESContainerChangedEvent& a_event,
    CompletionCallback a_onComplete
) {
    if (!AssignmentStore::ContainsSource(a_actor, a_event.baseObj)) {
        return;
    }

    auto const* actor = Core::ResolveActor(a_actor);
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
