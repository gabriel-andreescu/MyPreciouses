#include "Selection.h"

#include "Forms.h"
#include "RingFootprints.h"
#include "RingSounds.h"
#include "UI.h"
#include "VirtualRings.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

namespace Selection {
namespace {
    std::mutex g_lock;
    Snapshot g_snapshot;

    [[nodiscard]] RE::PlayerCharacter* GetPlayer() {
        return RE::PlayerCharacter::GetSingleton();
    }

    [[nodiscard]] RE::TESObjectARMO* LookupSourceRing(const RE::FormID a_sourceFormID) {
        return Inventory::AsRing(RE::TESForm::LookupByID(a_sourceFormID));
    }

    [[nodiscard]] RE::FormID ResolveFormID(
        SKSE::SerializationInterface& a_intfc,
        const RE::FormID a_formID,
        const std::string_view a_label
    ) {
        if (a_formID == 0) {
            return 0;
        }

        RE::FormID resolvedFormID = 0;
        if (!a_intfc.ResolveFormID(a_formID, resolvedFormID)) {
            logger::warn("Serialization: virtual ring resolve failed | field={} | form={:08X}", a_label, a_formID);
            return 0;
        }
        return resolvedFormID;
    }

    [[nodiscard]] bool CanUseVirtualTarget(const RingTarget a_target, const std::string_view a_action) {
        if (IsVirtualRingTarget(a_target)) {
            return true;
        }

        logger::warn("Selection: virtual target rejected | action={} | target={}", a_action, TargetLabel(a_target));
        return false;
    }

    [[nodiscard]] RingFootprints::RingTargetMask GetOccupiedTargets(
        const State& a_selection,
        const RingTarget a_target
    ) {
        if (const auto* ring = LookupSourceRing(a_selection.sourceFormID)) {
            return RingFootprints::GetOccupiedTargets(*ring, a_target);
        }

        RingFootprints::RingTargetMask mask;
        mask.Add(a_target);
        return mask;
    }

    [[nodiscard]] std::vector<RingTarget> FindConflictingTargets(
        const Snapshot& a_snapshot,
        const RingTarget a_target,
        const RingFootprints::RingTargetMask& a_occupiedTargets
    ) {
        std::vector<RingTarget> conflicts;
        for (const auto target : kVirtualRingTargets) {
            if (target == a_target) {
                continue;
            }

            const auto& selection = a_snapshot.targets[ToIndex(target)];
            if (selection.kind == Kind::kNone || selection.sourceFormID == 0) {
                continue;
            }

            if (a_occupiedTargets.Intersects(GetOccupiedTargets(selection, target))) {
                conflicts.push_back(target);
            }
        }

        return conflicts;
    }

    [[nodiscard]] bool CanOccupyTargets(
        const RingFootprints::RingTargetMask& a_occupiedTargets,
        const RingTarget a_target,
        const RE::TESObjectARMO& a_ring,
        const std::string_view a_action
    ) {
        if (!a_occupiedTargets.Empty()) {
            return true;
        }

        logger::warn(
            "Selection: virtual target rejected | action={} | target={} | source={:08X} | reason=invalidFootprintAnchor",
            a_action,
            TargetLabel(a_target),
            a_ring.GetFormID()
        );
        return false;
    }

    void ClearConflictingSelections(const std::vector<RingTarget>& a_conflicts) {
        for (const auto target : a_conflicts) {
            g_snapshot.targets[ToIndex(target)] = {};
        }
    }

    void ClearVanillaRingSlotConflict(RingActionResult& a_result, const RingTarget a_target) {
        Clear(a_target);
        VirtualRings::Clear(a_target);
        a_result.selectionChanged = true;
    }

    void ClearVanillaRingSlotConflicts(
        RingActionResult& a_result,
        const RE::TESObjectARMO& a_ring,
        const std::optional<RingTarget> a_selectedTarget
    ) {
        const auto occupiedTargets = RingFootprints::GetOccupiedTargets(a_ring, kVanillaRingTarget);
        auto conflicts = FindConflictingTargets(GetSnapshot(), kVanillaRingTarget, occupiedTargets);
        if (a_selectedTarget && std::ranges::find(conflicts, *a_selectedTarget) == conflicts.end()) {
            conflicts.push_back(*a_selectedTarget);
        }

        for (const auto target : conflicts) {
            ClearVanillaRingSlotConflict(a_result, target);
        }
    }

    template <class Predicate>
    [[nodiscard]] std::optional<RE::FormID> ClearMoveSourceSelection(
        Snapshot& a_snapshot,
        const RingTarget a_target,
        const RingTarget a_moveSourceTarget,
        Predicate a_matches
    ) {
        if (!IsVirtualRingTarget(a_moveSourceTarget) || a_moveSourceTarget == a_target) {
            return std::nullopt;
        }

        auto& selection = a_snapshot.targets[ToIndex(a_moveSourceTarget)];
        if (!a_matches(selection)) {
            return std::nullopt;
        }

        const auto restoredEffectSourceFormID = selection.restoredEffectSourceFormID;
        selection = {};
        return restoredEffectSourceFormID;
    }

    void ClearVirtualSelection(RE::Actor& a_actor, const RE::TESObjectARMO& a_ring, const RingTarget a_target) {
        Clear(a_target);
        VirtualRings::Clear(a_target);
        UI::RefreshItemRowsForRing(a_actor, std::addressof(a_ring));
    }

    [[nodiscard]] std::uint32_t CountMatchingSelections(
        const RE::FormID a_sourceFormID,
        const std::optional<Inventory::CustomEnchantmentKey>& a_customKey,
        const std::optional<Inventory::ExtraListIdentity>& a_identity,
        const std::optional<RingTarget> a_excludedTarget = std::nullopt
    ) {
        const auto snapshot = GetSnapshot();
        auto count = std::uint32_t {0};
        for (const auto target : kVirtualRingTargets) {
            if (a_excludedTarget && *a_excludedTarget == target) {
                continue;
            }

            const auto& selection = snapshot.targets[ToIndex(target)];
            if (!a_customKey) {
                if (selection.MatchesForm(a_sourceFormID)) {
                    ++count;
                }
                continue;
            }

            if (selection.MatchesCustomEnchantment(a_sourceFormID, *a_customKey, a_identity)) {
                ++count;
            }
        }

        return count;
    }

    [[nodiscard]] std::optional<RingTarget> FindVirtualTargetForVanillaRingSlotEquip(
        RE::Actor& a_actor,
        const RE::TESObjectARMO& a_ring,
        const RE::ObjectEquipParams& a_params,
        std::optional<Inventory::CustomEnchantmentKey>& a_customKey,
        std::optional<Inventory::ExtraListIdentity>& a_identity
    ) {
        const auto sourceFormID = a_ring.GetFormID();
        const auto snapshot = GetSnapshot();
        for (const auto target : kVirtualRingTargets) {
            const auto& selection = snapshot.targets[ToIndex(target)];
            if (!selection.MatchesSource(sourceFormID)) {
                continue;
            }

            if (selection.kind == Kind::kCustomEnchantment) {
                const auto key = selection.GetCustomKey();
                const auto identity = selection.GetCustomIdentity();
                if (!a_params.extraDataList) {
                    continue;
                }

                if (!Inventory::MatchesCustomSelection(a_params.extraDataList, key, identity)) {
                    continue;
                }

                const auto sourceMatches = Inventory::FindSourceMatches(a_actor, a_ring, key, identity);
                const auto selectedCopies = CountMatchingSelections(sourceFormID, key, identity, target);
                if (std::cmp_greater(sourceMatches.count, selectedCopies + 1)) {
                    continue;
                }

                a_customKey = key;
                a_identity = identity;
                return target;
            }

            if (Inventory::HasCustomEnchantment(a_params.extraDataList)) {
                continue;
            }

            const auto sourceMatches = Inventory::FindFormOnlyMatches(a_actor, a_ring);
            const auto selectedCopies = CountMatchingSelections(sourceFormID, std::nullopt, std::nullopt, target);
            if (std::cmp_greater(sourceMatches.count, selectedCopies + 1)) {
                continue;
            }

            return target;
        }

        return std::nullopt;
    }

    [[nodiscard]] std::optional<RingTarget> FindVirtualTargetForVanillaRingSlotEquip(
        RE::Actor& a_actor,
        const RE::TESObjectARMO& a_ring,
        const std::optional<Inventory::CustomEnchantmentKey>& a_customKey,
        const std::optional<Inventory::ExtraListIdentity>& a_identity
    ) {
        const auto sourceFormID = a_ring.GetFormID();
        const auto snapshot = GetSnapshot();
        for (const auto target : kVirtualRingTargets) {
            const auto& selection = snapshot.targets[ToIndex(target)];
            if (!selection.MatchesSource(sourceFormID)) {
                continue;
            }

            if (a_customKey) {
                if (!selection.MatchesCustomEnchantment(sourceFormID, *a_customKey, a_identity)) {
                    continue;
                }

                const auto sourceMatches = Inventory::FindSourceMatches(a_actor, a_ring, *a_customKey, a_identity);
                const auto selectedCopies = CountMatchingSelections(sourceFormID, a_customKey, a_identity, target);
                if (std::cmp_greater(sourceMatches.count, selectedCopies + 1)) {
                    continue;
                }

                return target;
            }

            if (!selection.MatchesForm(sourceFormID)) {
                continue;
            }

            const auto sourceMatches = Inventory::FindFormOnlyMatches(a_actor, a_ring);
            const auto selectedCopies = CountMatchingSelections(sourceFormID, std::nullopt, std::nullopt, target);
            if (std::cmp_greater(sourceMatches.count, selectedCopies + 1)) {
                continue;
            }

            return target;
        }

        return std::nullopt;
    }

    [[nodiscard]] RE::FormID EquipSlotFormID(const RE::ObjectEquipParams& a_params) {
        return a_params.equipSlot ? a_params.equipSlot->GetFormID() : RE::FormID {0};
    }

    [[nodiscard]] bool IsInVanillaRingSlot(
        RE::Actor& a_actor,
        const RE::TESObjectARMO& a_ring,
        const std::optional<Inventory::CustomEnchantmentKey>& a_customKey,
        const std::optional<Inventory::ExtraListIdentity>& a_customIdentity
    ) {
        if (a_customKey) {
            return Inventory::FindSourceMatches(a_actor, a_ring, *a_customKey, a_customIdentity).rightWornExtraList
                   != nullptr;
        }

        return Inventory::FindFormOnlyMatches(a_actor, a_ring).rightWorn;
    }

    void MoveVirtualToVanillaRingSlot(
        const RE::FormID a_sourceFormID,
        std::optional<Inventory::CustomEnchantmentKey> a_customKey,
        std::optional<Inventory::ExtraListIdentity> a_customIdentity,
        const RE::FormID a_equipSlotFormID,
        const bool a_forceEquip,
        const RingTarget a_target
    ) {
        auto* player = GetPlayer();
        auto* ring = LookupSourceRing(a_sourceFormID);
        if (!player || !ring) {
            return;
        }

        RE::ExtraDataList* equipExtraList = nullptr;
        if (a_customKey) {
            const auto sourceMatches = Inventory::FindSourceMatches(*player, *ring, *a_customKey, a_customIdentity);
            equipExtraList = sourceMatches.firstExtraList;
            if (!equipExtraList) {
                ClearVirtualSelection(*player, *ring, a_target);
                return;
            }
        }

        const auto selection = Get(a_target);
        if ((!a_customKey && selection.MatchesForm(a_sourceFormID))
            || (a_customKey && selection.MatchesCustomEnchantment(a_sourceFormID, *a_customKey, a_customIdentity))) {
            Clear(a_target);
            VirtualRings::Clear(a_target);
        }

        auto* equipManager = RE::ActorEquipManager::GetSingleton();
        if (!equipManager) {
            UI::RefreshItemRowsForRing(*player, ring);
            return;
        }

        const auto* equipSlot = a_equipSlotFormID == 0 ? nullptr
                                                       : RE::TESForm::LookupByID<RE::BGSEquipSlot>(a_equipSlotFormID);
        if (a_equipSlotFormID != 0 && !equipSlot) {
            UI::RefreshItemRowsForRing(*player, ring);
            return;
        }

        equipManager->EquipObject(player, ring, equipExtraList, 1, equipSlot, true, a_forceEquip, false, true);
        if (IsInVanillaRingSlot(*player, *ring, a_customKey, a_customIdentity)) {
            RingSounds::Play(*player, *ring, RingSounds::Event::kEquip);
        }
        UI::QueueInventoryMenuRefreshAfterVanillaRingSlotMove();
    }

    [[nodiscard]] bool UnequipVanillaRingSlot(
        RE::PlayerCharacter& a_player,
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
            std::addressof(a_player),
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
        RE::PlayerCharacter& a_player,
        RE::TESObjectARMO& a_ring,
        RE::ExtraDataList* a_extraList
    ) {
        auto* equipManager = RE::ActorEquipManager::GetSingleton();
        if (!equipManager) {
            return false;
        }

        auto* equipSlot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(Forms::kRightHandEquipSlotFormID);
        equipManager->EquipObject(
            std::addressof(a_player),
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

    [[nodiscard]] RingActionResult ToggleVanillaRingSlot(
        const RE::FormID a_sourceFormID,
        const std::optional<Inventory::CustomEnchantmentKey>& a_customKey,
        const std::optional<Inventory::ExtraListIdentity>& a_customIdentity
    ) {
        RingActionResult result;
        auto* player = GetPlayer();
        auto* ring = LookupSourceRing(a_sourceFormID);
        if (!player || !ring) {
            return result;
        }

        RE::ExtraDataList* rightWornExtraList = nullptr;
        RE::ExtraDataList* equipExtraList = nullptr;
        auto rightWorn = false;
        auto rightWornProtected = false;
        if (a_customKey) {
            const auto sourceMatches = Inventory::FindSourceMatches(*player, *ring, *a_customKey, a_customIdentity);
            if (!sourceMatches.HasMatch()) {
                return result;
            }

            rightWornExtraList = sourceMatches.rightWornExtraList;
            equipExtraList = sourceMatches.firstExtraList;
            rightWorn = rightWornExtraList != nullptr;
            rightWornProtected = sourceMatches.rightWornProtected;
        } else {
            const auto sourceMatches = Inventory::FindFormOnlyMatches(*player, *ring);
            if (!sourceMatches.HasMatch()) {
                return result;
            }

            rightWornExtraList = sourceMatches.rightWornExtraList;
            rightWorn = sourceMatches.rightWorn;
            rightWornProtected = sourceMatches.rightWornProtected;
        }

        if (rightWorn) {
            if (rightWornProtected) {
                return result;
            }

            if (!UnequipVanillaRingSlot(*player, *ring, rightWornExtraList)) {
                return result;
            }

            result.inventoryChanged = true;
            if (!IsInVanillaRingSlot(*player, *ring, a_customKey, a_customIdentity)) {
                RingSounds::Play(*player, *ring, RingSounds::Event::kUnequip);
            }
            return result;
        }

        const auto target = FindVirtualTargetForVanillaRingSlotEquip(*player, *ring, a_customKey, a_customIdentity);
        if (!EquipVanillaRingSlot(*player, *ring, equipExtraList)) {
            return result;
        }

        ClearVanillaRingSlotConflicts(result, *ring, target);
        result.inventoryChanged = true;
        if (IsInVanillaRingSlot(*player, *ring, a_customKey, a_customIdentity)) {
            RingSounds::Play(*player, *ring, RingSounds::Event::kEquip);
        }
        return result;
    }

    void QueueMoveVirtualToVanillaRingSlot(
        const RE::FormID a_sourceFormID,
        std::optional<Inventory::CustomEnchantmentKey> a_customKey,
        std::optional<Inventory::ExtraListIdentity> a_customIdentity,
        const RE::ObjectEquipParams& a_params,
        const RingTarget a_target
    ) {
        stl::add_task([a_sourceFormID,
                          customKey = std::move(a_customKey),
                          customIdentity = a_customIdentity,
                          equipSlotFormID = EquipSlotFormID(a_params),
                          forceEquip = a_params.forceEquip,
                          a_target] {
            MoveVirtualToVanillaRingSlot(
                a_sourceFormID,
                customKey,
                customIdentity,
                equipSlotFormID,
                forceEquip,
                a_target
            );
        });
    }

    void EnforceTargetInvariant(RE::Actor& a_actor, const RingTarget a_target) {
        const auto selection = Get(a_target);
        auto* ring = GetSource(a_target);
        if (!ring || selection.kind == Kind::kNone) {
            return;
        }

        const auto occupiedTargets = RingFootprints::GetOccupiedTargets(*ring, a_target);
        if (occupiedTargets.Empty()) {
            ClearVirtualSelection(a_actor, *ring, a_target);
            return;
        }

        if (occupiedTargets.Contains(kVanillaRingTarget) && Inventory::HasRightWornRing(a_actor)) {
            ClearVirtualSelection(a_actor, *ring, a_target);
            return;
        }

        if (selection.kind == Kind::kCustomEnchantment) {
            const auto key = selection.GetCustomKey();
            const auto identity = selection.GetCustomIdentity();
            const auto sourceMatches = Inventory::FindSourceMatches(a_actor, *ring, key, identity);
            const auto selectedCopies = CountMatchingSelections(ring->GetFormID(), key, identity);
            const auto vanillaSlotClaim = sourceMatches.rightWornExtraList ? 1u : 0u;
            const auto requiredMatchingCount = selectedCopies + vanillaSlotClaim;
            const auto shouldClear = !sourceMatches.HasMatch()
                                     || std::cmp_less(sourceMatches.count, requiredMatchingCount);
            if (shouldClear) {
                ClearVirtualSelection(a_actor, *ring, a_target);
            }
            return;
        }

        const auto sourceMatches = Inventory::FindFormOnlyMatches(a_actor, *ring);
        const auto selectedCopies = CountMatchingSelections(ring->GetFormID(), std::nullopt, std::nullopt);
        const auto vanillaSlotClaim = sourceMatches.rightWorn ? 1u : 0u;
        const auto requiredInventoryCount = selectedCopies + vanillaSlotClaim;
        const auto shouldClear = !sourceMatches.HasMatch()
                                 || std::cmp_less(sourceMatches.count, requiredInventoryCount);
        if (shouldClear) {
            ClearVirtualSelection(a_actor, *ring, a_target);
        }
    }

    [[nodiscard]] bool SnapshotReferencesBaseObject(const Snapshot& a_snapshot, const RE::FormID a_baseObject) {
        return std::ranges::any_of(a_snapshot.targets, [a_baseObject](const auto& a_selection) {
            return a_selection.sourceFormID == a_baseObject;
        });
    }
}

bool Set(RE::TESObjectARMO* a_ring, const RingTarget a_target, const std::optional<RingTarget> a_moveSourceTarget) {
    if (!CanUseVirtualTarget(a_target, "set"sv)) {
        return false;
    }

    if (!a_ring) {
        Clear(a_target);
        return true;
    }

    const auto occupiedTargets = RingFootprints::GetOccupiedTargets(*a_ring, a_target);
    if (!CanOccupyTargets(occupiedTargets, a_target, *a_ring, "set"sv)) {
        return false;
    }

    const auto conflicts = FindConflictingTargets(GetSnapshot(), a_target, occupiedTargets);

    std::scoped_lock lock(g_lock);
    auto& selection = g_snapshot.targets[ToIndex(a_target)];
    auto restoredEffectSourceFormID = selection.sourceFormID == a_ring->GetFormID()
                                          ? selection.restoredEffectSourceFormID
                                          : RE::FormID {0};
    if (a_moveSourceTarget) {
        const auto movedEffectSourceFormID = ClearMoveSourceSelection(
            g_snapshot,
            a_target,
            *a_moveSourceTarget,
            [formID = a_ring->GetFormID()](const State& a_selection) {
                return a_selection.MatchesForm(formID);
            }
        );
        if (!movedEffectSourceFormID) {
            return false;
        }

        restoredEffectSourceFormID = *movedEffectSourceFormID;
    }
    ClearConflictingSelections(conflicts);
    selection = State {
        .kind = Kind::kFormOnly,
        .sourceFormID = a_ring->GetFormID(),
        .restoredEffectSourceFormID = restoredEffectSourceFormID,
    };
    return true;
}

bool SetCustom(
    RE::TESObjectARMO& a_ring,
    Inventory::CustomEnchantmentKey a_key,
    std::optional<Inventory::ExtraListIdentity> a_identity,
    const RingTarget a_target,
    const std::optional<RingTarget> a_moveSourceTarget
) {
    if (!CanUseVirtualTarget(a_target, "setCustom"sv)) {
        return false;
    }

    const auto occupiedTargets = RingFootprints::GetOccupiedTargets(a_ring, a_target);
    if (!CanOccupyTargets(occupiedTargets, a_target, a_ring, "setCustom"sv)) {
        return false;
    }

    const auto conflicts = FindConflictingTargets(GetSnapshot(), a_target, occupiedTargets);

    std::scoped_lock lock(g_lock);
    const auto& oldSelection = g_snapshot.targets[ToIndex(a_target)];
    auto restoredEffectSourceFormID = oldSelection.sourceFormID == a_ring.GetFormID()
                                          ? oldSelection.restoredEffectSourceFormID
                                          : RE::FormID {0};
    if (a_moveSourceTarget) {
        const auto movedEffectSourceFormID = ClearMoveSourceSelection(
            g_snapshot,
            a_target,
            *a_moveSourceTarget,
            [formID = a_ring.GetFormID(), &a_key, &a_identity](const State& a_selection) {
                return a_selection.MatchesCustomEnchantment(formID, a_key, a_identity);
            }
        );
        if (!movedEffectSourceFormID) {
            return false;
        }

        restoredEffectSourceFormID = *movedEffectSourceFormID;
    }
    ClearConflictingSelections(conflicts);
    g_snapshot.targets[ToIndex(a_target)] = State {
        .kind = Kind::kCustomEnchantment,
        .sourceFormID = a_ring.GetFormID(),
        .restoredEffectSourceFormID = restoredEffectSourceFormID,
        .customKey = std::move(a_key),
        .customIdentity = a_identity,
    };
    return true;
}

void Clear(const RingTarget a_target) {
    std::scoped_lock lock(g_lock);
    g_snapshot.targets[ToIndex(a_target)] = {};
}

void SetRestoredEffectSourceFormID(const RingTarget a_target, const RE::FormID a_effectSourceFormID) {
    if (!CanUseVirtualTarget(a_target, "setRestoredEffectSource"sv)) {
        return;
    }

    std::scoped_lock lock(g_lock);
    g_snapshot.targets[ToIndex(a_target)].restoredEffectSourceFormID = a_effectSourceFormID;
}

RE::TESObjectARMO* GetSource(const RingTarget a_target) {
    if (!IsVirtualRingTarget(a_target)) {
        return nullptr;
    }

    return LookupSourceRing(GetFormID(a_target));
}

RE::FormID GetFormID(const RingTarget a_target) {
    if (!IsVirtualRingTarget(a_target)) {
        return 0;
    }

    std::scoped_lock lock(g_lock);
    return g_snapshot.targets[ToIndex(a_target)].sourceFormID;
}

State Get(const RingTarget a_target) {
    if (!IsVirtualRingTarget(a_target)) {
        return {};
    }

    std::scoped_lock lock(g_lock);
    return g_snapshot.targets[ToIndex(a_target)];
}

bool IsSelected(
    const RingTarget a_target,
    const RE::FormID a_sourceFormID,
    const std::optional<Inventory::CustomEnchantmentKey>& a_customKey,
    const std::optional<Inventory::ExtraListIdentity>& a_identity
) {
    const auto selection = Get(a_target);
    if (a_customKey) {
        return selection.MatchesCustomEnchantment(a_sourceFormID, *a_customKey, a_identity);
    }

    return selection.MatchesForm(a_sourceFormID);
}

void Load(const Snapshot& a_state, SKSE::SerializationInterface& a_intfc) {
    Snapshot nextSnapshot;
    for (const auto target : kVirtualRingTargets) {
        const auto& storedState = a_state.targets[ToIndex(target)];
        auto& nextState = nextSnapshot.targets[ToIndex(target)];
        if (storedState.kind == Kind::kNone || storedState.sourceFormID == 0) {
            continue;
        }

        const auto sourceFormID = ResolveFormID(a_intfc, storedState.sourceFormID, "source form"sv);
        if (sourceFormID == 0) {
            continue;
        }

        const auto restoredEffectSourceFormID = ResolveFormID(
            a_intfc,
            storedState.restoredEffectSourceFormID,
            "effect source form"sv
        );

        if (storedState.kind == Kind::kFormOnly) {
            nextState = State {
                .kind = Kind::kFormOnly,
                .sourceFormID = sourceFormID,
                .restoredEffectSourceFormID = restoredEffectSourceFormID,
            };
            continue;
        }

        if (storedState.kind != Kind::kCustomEnchantment) {
            logger::warn(
                "Serialization: virtual selection cleared | target={} | source={:08X} | kind={} | reason=unsupportedKind",
                TargetLabel(target),
                sourceFormID,
                std::to_underlying(storedState.kind)
            );
            continue;
        }

        auto customKey = storedState.customKey;
        customKey.enchantmentFormID = ResolveFormID(a_intfc, customKey.enchantmentFormID, "custom enchantment"sv);
        if (customKey.enchantmentFormID
            == 0
            || !RE::TESForm::LookupByID<RE::EnchantmentItem>(customKey.enchantmentFormID)) {
            logger::warn(
                "Serialization: custom virtual selection cleared | target={} | source={:08X} | enchantment={:08X} | reason=enchantmentMissing",
                TargetLabel(target),
                sourceFormID,
                storedState.customKey.enchantmentFormID
            );
            continue;
        }

        auto customIdentity = storedState.customIdentity;
        if (customIdentity) {
            customIdentity->baseID = ResolveFormID(a_intfc, customIdentity->baseID, "custom unique base"sv);
            if (!customIdentity->IsValid()) {
                logger::warn(
                    "Serialization: custom virtual selection cleared | target={} | source={:08X} | reason=uniqueIDResolveFailed",
                    TargetLabel(target),
                    sourceFormID
                );
                continue;
            }
        }

        nextState = State {
            .kind = Kind::kCustomEnchantment,
            .sourceFormID = sourceFormID,
            .restoredEffectSourceFormID = restoredEffectSourceFormID,
            .customKey = std::move(customKey),
            .customIdentity = customIdentity,
        };
    }

    std::scoped_lock lock(g_lock);
    g_snapshot = std::move(nextSnapshot);
}

Snapshot GetSnapshot() {
    std::scoped_lock lock(g_lock);
    return g_snapshot;
}

void Revert() {
    std::scoped_lock lock(g_lock);
    g_snapshot = {};
}

RingActionResult MoveVanillaRingSlotFormToVirtual(const RE::FormID a_sourceFormID, const RingTarget a_target) {
    RingActionResult result;
    if (!CanUseVirtualTarget(a_target, "moveVanillaRingSlotFormToVirtual"sv)) {
        return result;
    }

    auto* player = GetPlayer();
    auto* ring = LookupSourceRing(a_sourceFormID);
    if (!player || !ring) {
        return result;
    }

    auto sourceMatches = Inventory::FindFormOnlyMatches(*player, *ring);
    if (!sourceMatches.HasMatch()) {
        if (GetFormID(a_target) == a_sourceFormID) {
            ClearVirtualSelection(*player, *ring, a_target);
        }
        return result;
    }

    const auto selectedCopies = CountMatchingSelections(a_sourceFormID, std::nullopt, std::nullopt, a_target);
    if (sourceMatches.rightWorn && std::cmp_less_equal(sourceMatches.count, selectedCopies + 1)) {
        if (!Inventory::UnequipRightWornSource(*player, *ring)) {
            return result;
        }

        result.inventoryChanged = true;
        sourceMatches = Inventory::FindFormOnlyMatches(*player, *ring);
        if (sourceMatches.rightWorn) {
            return result;
        }
    }

    if (!Set(ring, a_target)) {
        return result;
    }

    VirtualRings::RequestRefresh(
        VirtualRings::RefreshOptions {
            .soundTarget = a_target,
            .sound = RingSounds::Event::kEquip,
        }
    );

    result.selectionChanged = true;
    return result;
}

RingActionResult MoveVanillaRingSlotCustomToVirtual(
    const RE::FormID a_sourceFormID,
    const Inventory::CustomEnchantmentKey& a_customKey,
    const std::optional<Inventory::ExtraListIdentity> a_customIdentity,
    const RingTarget a_target
) {
    RingActionResult result;
    if (!CanUseVirtualTarget(a_target, "moveVanillaRingSlotCustomToVirtual"sv)) {
        return result;
    }

    auto* player = GetPlayer();
    auto* ring = LookupSourceRing(a_sourceFormID);
    if (!player || !ring) {
        return result;
    }

    auto sourceMatches = Inventory::FindSourceMatches(*player, *ring, a_customKey, a_customIdentity);
    if (!sourceMatches.HasMatch()) {
        return result;
    }

    const auto selectedCopies = CountMatchingSelections(a_sourceFormID, a_customKey, a_customIdentity, a_target);
    if (sourceMatches.rightWornExtraList && std::cmp_less_equal(sourceMatches.count, selectedCopies + 1)) {
        if (sourceMatches.rightWornProtected) {
            return result;
        }

        auto* equipManager = RE::ActorEquipManager::GetSingleton();
        if (!equipManager) {
            return result;
        }

        equipManager->UnequipObject(
            player,
            ring,
            sourceMatches.rightWornExtraList,
            1,
            nullptr,
            true,
            false,
            false,
            true,
            nullptr
        );
        result.inventoryChanged = true;

        sourceMatches = Inventory::FindSourceMatches(*player, *ring, a_customKey, a_customIdentity);
        if (!sourceMatches.HasMatch()
            || (sourceMatches.rightWornExtraList && std::cmp_less_equal(sourceMatches.count, selectedCopies + 1))) {
            return result;
        }
    }

    if (!SetCustom(*ring, a_customKey, a_customIdentity, a_target)) {
        return result;
    }

    VirtualRings::RequestRefresh(
        VirtualRings::RefreshOptions {
            .soundTarget = a_target,
            .sound = RingSounds::Event::kEquip,
        }
    );

    result.selectionChanged = true;
    return result;
}

void QueueVanillaRingSlotFormToVirtual(const RE::FormID a_sourceFormID, const RingTarget a_target) {
    if (!CanUseVirtualTarget(a_target, "queueVanillaRingSlotFormToVirtual"sv)) {
        return;
    }

    stl::add_task([a_sourceFormID, a_target] {
        const auto result = MoveVanillaRingSlotFormToVirtual(a_sourceFormID, a_target);
        auto* player = GetPlayer();
        auto* ring = LookupSourceRing(a_sourceFormID);
        if (!player || !ring) {
            return;
        }

        if (result.inventoryChanged) {
            UI::RefreshItemRowsForRing(*player, ring);
        } else if (result.selectionChanged) {
            UI::RefreshRingRows();
        }
    });
}

void QueueVanillaRingSlotCustomToVirtual(
    const RE::FormID a_sourceFormID,
    Inventory::CustomEnchantmentKey a_key,
    std::optional<Inventory::ExtraListIdentity> a_identity,
    const RingTarget a_target
) {
    if (!CanUseVirtualTarget(a_target, "queueVanillaRingSlotCustomToVirtual"sv)) {
        return;
    }

    stl::add_task([a_sourceFormID, key = std::move(a_key), identity = a_identity, a_target] {
        const auto result = MoveVanillaRingSlotCustomToVirtual(a_sourceFormID, key, identity, a_target);
        auto* player = GetPlayer();
        auto* ring = LookupSourceRing(a_sourceFormID);
        if (!player || !ring) {
            return;
        }

        if (result.inventoryChanged) {
            UI::RefreshItemRowsForRing(*player, ring);
        } else if (result.selectionChanged) {
            UI::RefreshRingRows();
        }
    });
}

RingActionResult ToggleVanillaRingSlotForm(const RE::FormID a_sourceFormID) {
    return ToggleVanillaRingSlot(a_sourceFormID, std::nullopt, std::nullopt);
}

RingActionResult ToggleVanillaRingSlotCustom(
    const RE::FormID a_sourceFormID,
    const Inventory::CustomEnchantmentKey& a_customKey,
    const std::optional<Inventory::ExtraListIdentity> a_customIdentity
) {
    return ToggleVanillaRingSlot(a_sourceFormID, a_customKey, a_customIdentity);
}

bool InterceptRightEquip(RE::Actor& a_actor, const RE::TESObjectARMO& a_ring, const RE::ObjectEquipParams& a_params) {
    if (a_params.equipSlot && a_params.equipSlot->GetFormID() == Forms::kLeftHandEquipSlotFormID) {
        return false;
    }

    std::optional<Inventory::CustomEnchantmentKey> customKey;
    std::optional<Inventory::ExtraListIdentity> customIdentity;
    const auto
        selectedTarget = FindVirtualTargetForVanillaRingSlotEquip(a_actor, a_ring, a_params, customKey, customIdentity);
    if (!selectedTarget) {
        return false;
    }

    QueueMoveVirtualToVanillaRingSlot(
        a_ring.GetFormID(),
        std::move(customKey),
        customIdentity,
        a_params,
        *selectedTarget
    );
    return true;
}

void QueueCheck() {
    stl::add_task([] {
        auto* player = GetPlayer();
        if (!player) {
            return;
        }

        for (const auto target : kVirtualRingTargets) {
            EnforceTargetInvariant(*player, target);
        }

        VirtualRings::RequestRefresh();
    });
}

void OnContainerChanged(const RE::TESContainerChangedEvent& a_event) {
    const auto snapshot = GetSnapshot();
    if (!SnapshotReferencesBaseObject(snapshot, a_event.baseObj)) {
        return;
    }

    auto* player = GetPlayer();
    if (!player) {
        return;
    }

    const auto playerFormID = player->GetFormID();
    if (a_event.oldContainer != playerFormID && a_event.newContainer != playerFormID) {
        return;
    }

    QueueCheck();
}
}
