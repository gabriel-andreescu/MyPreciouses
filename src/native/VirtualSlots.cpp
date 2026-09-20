#include "VirtualSlots.h"

#include <RE/Skyrim.h> // IWYU pragma: keep
#include <SKSE/SKSE.h> // IWYU pragma: keep

#include "Audio/EquipSounds.h"
#include "Compatibility/Vanilla.h"
#include "Core/ActorKey.h"
#include "Core/Assignment.h"
#include "Core/ItemSource.h"
#include "Core/Target.h"
#include "Core/TargetMask.h"
#include "Equipment/AssignmentStore.h"
#include "Equipment/RaceSwitchRestore.h"
#include "Inventory.h"
#include "Papyrus/ScriptEventMirror.h"
#include "Settings.h"
#include "SourceModelFootprints.h"
#include "VirtualSlots/EffectSources.h"
#include "VirtualSlots/EnchantmentEffects.h"
#include "Visuals/Attachments.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace VirtualSlots {
namespace {
    struct TargetState {
        RE::FormID sourceFormID {0};
        RE::TESObjectARMO* effectSource {nullptr};
        RE::EnchantmentItem* customEnchantment {nullptr};
        Core::Assignment activeAssignment;
        ExtraRingMode mode {ExtraRingMode::kFunctional};
        Core::TargetMask sourceTargets;
        bool active {false};
    };

    struct ActorState {
        std::array<TargetState, Core::kAllTargets.size()> targets;
        std::array<Audio::EquipSounds::Cue, Core::kAllTargets.size()> pendingSounds {};
        std::optional<std::uint32_t> appliedMagnitudeRingCount;
        bool refreshPending {false};
        bool refreshRunning {false};
        bool preserveLoadedEffects {false};
        bool reapplyEffects {false};
        bool restoreMissingEffects {false};
    };

    struct ClearAction {
        Core::ActorKey actorKey;
        RE::Actor* actor {nullptr};
        RE::TESObjectARMO* effectSource {nullptr};
        RE::FormID sourceFormID {0};
        RE::FormID effectSourceFormID {0};
        Audio::EquipSounds::Cue sound {Audio::EquipSounds::Cue::kNone};
        ScriptBindingClearMode scriptBindings {ScriptBindingClearMode::kRelease};
        bool dispatchUnequipped {false};
        bool active {false};
    };

    struct ApplyAction {
        RE::Actor* actor {nullptr};
        RE::TESObjectARMO* effectSource {nullptr};
        RE::EnchantmentItem* customEnchantment {nullptr};
        RE::FormID sourceFormID {0};
        Core::ItemSource itemSource;
        Audio::EquipSounds::Cue sound {Audio::EquipSounds::Cue::kNone};
        bool dispatchEquipped {false};
    };

    struct RefreshState {
        std::optional<std::uint32_t> appliedMagnitudeRingCount;
        bool preserveLoadedEffects {false};
        bool reapplyEffects {false};
        bool restoreMissingEffects {false};
    };

    struct VanillaRingSlotState {
        RE::TESObjectARMO* ring {nullptr};
        RE::ExtraDataList* extraList {nullptr};
        bool hasMagnitudeEnchantment {false};
    };

    struct TargetRefreshPlan {
        Core::ActorKey actor;
        Core::Target target {Core::kDefaultLeftTarget};
        Core::Assignment expectedAssignment;
        std::vector<ClearAction> clears;
        ApplyAction apply;
        RE::FormID retainedEffectSourceFormID {0};
        bool clearAssignment {false};
        bool updateRestoredEffectSource {false};
    };

    std::mutex g_lock;

    [[nodiscard]] std::unordered_map<Core::ActorKey, ActorState>& ActorStates() {
        static auto* states = new std::unordered_map<Core::ActorKey, ActorState>();
        return *states;
    }

    [[nodiscard]] ActorState& GetOrCreateActorState(const Core::ActorKey a_actor) {
        return ActorStates()[a_actor];
    }

    [[nodiscard]] ActorState* FindActorState(const Core::ActorKey a_actor) {
        auto& states = ActorStates();
        if (const auto actorState = states.find(a_actor); actorState != states.end()) {
            return std::addressof(actorState->second);
        }

        return nullptr;
    }

    [[nodiscard]] bool HasApplyAction(const ApplyAction& a_action) {
        return a_action.actor != nullptr && a_action.effectSource != nullptr;
    }

    [[nodiscard]] bool SourceMatchesGetEquippedArgument(
        const RE::FormID a_sourceFormID,
        RE::TESForm& a_getEquippedArgument
    ) {
        if (a_sourceFormID == 0) {
            return false;
        }

        if (auto const* list = a_getEquippedArgument.As<RE::BGSListForm>()) {
            return list->HasForm(a_sourceFormID);
        }

        return a_getEquippedArgument.GetFormID() == a_sourceFormID;
    }

    [[nodiscard]] bool SourceMatchesWornHasKeywordArgument(
        const RE::FormID a_sourceFormID,
        RE::BGSKeyword& a_wornHasKeywordArgument
    ) {
        if (a_sourceFormID == 0) {
            return false;
        }

        auto const* source = Inventory::AsRing(RE::TESForm::LookupByID(a_sourceFormID));
        return source != nullptr && source->HasKeyword(std::addressof(a_wornHasKeywordArgument));
    }

    [[nodiscard]] bool HasCountedMagnitudeEnchantment(const TargetState& a_state) {
        const auto functional = a_state.mode == ExtraRingMode::kFunctional;
        return a_state.active
               && functional
               && a_state.effectSource
               != nullptr
               && VirtualSlots::EnchantmentEffects::HasMagnitudeEnchantment(
                   a_state.effectSource->formEnchanting ? a_state.effectSource->formEnchanting
                                                        : a_state.customEnchantment
               );
    }

    [[nodiscard]] bool HasEffectSource(const TargetState& a_state, const RE::TESObjectARMO* a_armor) {
        return a_armor != nullptr && a_state.effectSource == a_armor;
    }

    [[nodiscard]] bool IsCountedVirtualEffectSource(const Core::ActorKey a_actor, const RE::TESObjectARMO* a_armor) {
        if (!a_actor || !a_armor) {
            return false;
        }

        std::scoped_lock const lock(g_lock);
        const auto* actorState = FindActorState(a_actor);
        if (!actorState) {
            return false;
        }

        return std::ranges::any_of(Core::kVirtualTargets, [actorState, a_armor](const auto a_target) {
            const auto& state = actorState->targets[Core::ToIndex(a_target)];
            return HasCountedMagnitudeEnchantment(state) && HasEffectSource(state, a_armor);
        });
    }

    [[nodiscard]] std::uint32_t CountActiveMagnitudeVirtualRings(const ActorState& a_actorState) {
        auto count = std::uint32_t {0};
        for (const auto target : Core::kVirtualTargets) {
            if (HasCountedMagnitudeEnchantment(a_actorState.targets[Core::ToIndex(target)])) {
                ++count;
            }
        }

        return count;
    }

    [[nodiscard]] std::uint32_t CountActiveMagnitudeVirtualRings(const Core::ActorKey a_actor) {
        if (!a_actor) {
            return 0;
        }

        std::scoped_lock const lock(g_lock);
        const auto* actorState = FindActorState(a_actor);
        return actorState ? CountActiveMagnitudeVirtualRings(*actorState) : 0;
    }

    [[nodiscard]] RE::TESObjectARMO* GetVanillaRingSlotArmor(RE::Actor& a_actor) {
        auto* armor = a_actor.GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kRing);
        return Inventory::IsRing(armor) ? armor : nullptr;
    }

    [[nodiscard]] VanillaRingSlotState GetVanillaRingSlotState(RE::Actor& a_actor) {
        auto* ring = GetVanillaRingSlotArmor(a_actor);
        if (!ring) {
            return {};
        }

        const auto state = Inventory::GetRingInventoryState(a_actor, *ring);
        if (!state.rightWorn) {
            return {};
        }

        return VanillaRingSlotState {
            .ring = ring,
            .extraList = state.rightWornExtraList,
            .hasMagnitudeEnchantment = VirtualSlots::EnchantmentEffects::HasMagnitudeEnchantment(
                *ring,
                state.rightWornExtraList
            ),
        };
    }

    [[nodiscard]] std::uint32_t CountEquippedMagnitudeRings(RE::Actor& a_actor) {
        auto count = CountActiveMagnitudeVirtualRings(Core::MakeActorKey(a_actor));
        const auto vanillaSlot = GetVanillaRingSlotState(a_actor);
        if (vanillaSlot.hasMagnitudeEnchantment) {
            ++count;
        }

        return count;
    }

    [[nodiscard]] bool SourceHasCountedMagnitudeEnchantment(RE::Actor& a_actor, const RE::TESObjectARMO* a_source) {
        if (!a_source) {
            return false;
        }

        if (IsCountedVirtualEffectSource(Core::MakeActorKey(a_actor), a_source)) {
            return true;
        }

        const auto vanillaSlot = GetVanillaRingSlotState(a_actor);
        return vanillaSlot.hasMagnitudeEnchantment && vanillaSlot.ring == a_source;
    }

    void ReapplyVanillaRingSlotEffects(RE::Actor& a_actor) {
        const auto vanillaSlot = GetVanillaRingSlotState(a_actor);
        if (!vanillaSlot.hasMagnitudeEnchantment || !vanillaSlot.ring) {
            return;
        }

        VirtualSlots::EnchantmentEffects::DispelSourceEffects(a_actor, *vanillaSlot.ring);
        a_actor.UpdateArmorAbility(vanillaSlot.ring, vanillaSlot.extraList);
    }

    [[nodiscard]] ClearAction ExtractClearAction(
        const Core::ActorKey a_actorKey,
        RE::Actor* a_actor,
        TargetState& a_state,
        const bool a_dispatchUnequipped,
        const Audio::EquipSounds::Cue a_sound = Audio::EquipSounds::Cue::kNone,
        const ScriptBindingClearMode a_scriptBindings = ScriptBindingClearMode::kRelease
    ) {
        if (!a_state.effectSource) {
            a_state = {};
            return {};
        }

        const ClearAction action {
            .actorKey = a_actorKey,
            .actor = a_actor,
            .effectSource = a_state.effectSource,
            .sourceFormID = a_state.sourceFormID,
            .effectSourceFormID = a_state.effectSource->GetFormID(),
            .sound = a_sound,
            .scriptBindings = a_scriptBindings,
            .dispatchUnequipped = a_dispatchUnequipped,
            .active = a_state.active,
        };

        a_state = {};
        return action;
    }

    void MergeClearAction(TargetRefreshPlan& a_plan, ClearAction a_clear) {
        if (!a_clear.effectSource || a_clear.effectSourceFormID == 0) {
            return;
        }

        auto const existing = std::ranges::find_if(a_plan.clears, [&](const auto& a_existing) {
            return a_existing.effectSourceFormID == a_clear.effectSourceFormID;
        });
        if (existing == a_plan.clears.end()) {
            a_plan.clears.push_back(a_clear);
            return;
        }

        if (!existing->actor) {
            existing->actor = a_clear.actor;
        }
        if (existing->sourceFormID == 0) {
            existing->sourceFormID = a_clear.sourceFormID;
        }
        if (existing->sound == Audio::EquipSounds::Cue::kNone) {
            existing->sound = a_clear.sound;
        }
        if (a_clear.scriptBindings == ScriptBindingClearMode::kRelease) {
            existing->scriptBindings = ScriptBindingClearMode::kRelease;
        }
        existing->dispatchUnequipped = existing->dispatchUnequipped || a_clear.dispatchUnequipped;
        existing->active = existing->active || a_clear.active;
    }

    void RunClearAction(const ClearAction& a_action) {
        if (!a_action.effectSource || a_action.effectSourceFormID == 0) {
            return;
        }

        if (a_action.actor) {
            VirtualSlots::EnchantmentEffects::DispelSourceEffects(*a_action.actor, *a_action.effectSource);
        }

        if (a_action.dispatchUnequipped && a_action.actor && a_action.active) {
            if (a_action.scriptBindings == ScriptBindingClearMode::kSuspend) {
                Papyrus::ScriptEventMirror::SuspendEffectSourceBindingsForUnequip(
                    a_action.effectSourceFormID,
                    *a_action.actor
                );
            } else {
                Papyrus::ScriptEventMirror::RemoveEffectSourceBindingsForUnequip(
                    a_action.effectSourceFormID,
                    *a_action.actor
                );
            }
        } else {
            Papyrus::ScriptEventMirror::RemoveEffectSourceBindings(a_action.actorKey, a_action.effectSourceFormID);
        }

        if (a_action.actor && a_action.active) {
            Audio::EquipSounds::Play(*a_action.actor, a_action.sourceFormID, a_action.sound);
        }
    }

    void RunApplyAction(const ApplyAction& a_action) {
        if (!a_action.actor || !a_action.effectSource) {
            return;
        }

        VirtualSlots::EnchantmentEffects::DispelSourceEffects(*a_action.actor, *a_action.effectSource);
        VirtualSlots::EnchantmentEffects::ApplyEffectSourceEnchantment(
            *a_action.actor,
            *a_action.effectSource,
            a_action.customEnchantment
        );

        if (a_action.dispatchEquipped) {
            auto const* sourceRing = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_action.sourceFormID);
            if (sourceRing) {
                static_cast<void>(Papyrus::ScriptEventMirror::DispatchEquipped(
                    *a_action.actor,
                    *sourceRing,
                    a_action.itemSource,
                    *a_action.effectSource
                ));
            }
        }

        Audio::EquipSounds::Play(*a_action.actor, a_action.sourceFormID, a_action.sound);
    }

    [[nodiscard]] ApplyAction MakeApplyAction(
        RE::Actor& a_actor,
        const TargetState& a_state,
        const RE::FormID a_sourceFormID,
        const Audio::EquipSounds::Cue a_sound = Audio::EquipSounds::Cue::kNone,
        const bool a_dispatchEquipped = false
    ) {
        if (!a_state.effectSource) {
            return {};
        }

        return ApplyAction {
            .actor = std::addressof(a_actor),
            .effectSource = a_state.effectSource,
            .customEnchantment = a_state.customEnchantment,
            .sourceFormID = a_sourceFormID,
            .itemSource = a_state.activeAssignment.source,
            .sound = a_sound,
            .dispatchEquipped = a_dispatchEquipped,
        };
    }

    [[nodiscard]] std::array<RE::FormID, Core::kAllTargets.size()> SnapshotFunctionalVirtualSourceFormIDs(
        const Core::ActorKey a_actor
    ) {
        std::array<RE::FormID, Core::kAllTargets.size()> sourceFormIDs {};
        if (!a_actor) {
            return sourceFormIDs;
        }

        std::scoped_lock const lock(g_lock);
        const auto* actorState = FindActorState(a_actor);
        if (!actorState) {
            return sourceFormIDs;
        }

        for (const auto target : Core::kVirtualTargets) {
            const auto& state = actorState->targets[Core::ToIndex(target)];
            if (!state.active || state.mode != ExtraRingMode::kFunctional || state.sourceFormID == 0) {
                continue;
            }

            sourceFormIDs[Core::ToIndex(target)] = state.sourceFormID;
        }

        return sourceFormIDs;
    }

    void RefreshVanillaCompatibility(const Core::ActorKey a_actor) {
        const auto sourceFormIDs = SnapshotFunctionalVirtualSourceFormIDs(a_actor);
        Compatibility::Vanilla::RefreshFrostmoonVirtualRings(
            a_actor,
            std::span<const RE::FormID> {sourceFormIDs.data(), sourceFormIDs.size()}
        );
    }

    void StoreRefreshOptions(ActorState& a_actorState, const RefreshOptions& a_options) {
        if (a_options.sound
            != Audio::EquipSounds::Cue::kNone
            && a_options.soundTarget
            && Core::IsVirtualTarget(*a_options.soundTarget)) {
            a_actorState.pendingSounds[Core::ToIndex(*a_options.soundTarget)] = a_options.sound;
        }

        a_actorState.preserveLoadedEffects = a_actorState.preserveLoadedEffects || a_options.preserveLoadedEffects;
        a_actorState.reapplyEffects = a_actorState.reapplyEffects || a_options.reapplyEffects;
        a_actorState.restoreMissingEffects = a_actorState.restoreMissingEffects || a_options.restoreMissingEffects;
    }

    [[nodiscard]] Audio::EquipSounds::Cue ConsumePendingSound(ActorState& a_actorState, const Core::Target a_target) {
        auto& sound = a_actorState.pendingSounds[Core::ToIndex(a_target)];
        const auto result = sound;
        sound = Audio::EquipSounds::Cue::kNone;
        return result;
    }

    [[nodiscard]] RE::TESObjectARMO* LookupItemSource(const Core::Assignment& a_assignment) {
        if (!a_assignment.IsAssigned()) {
            return nullptr;
        }

        return Inventory::AsRing(RE::TESForm::LookupByID(a_assignment.source.sourceFormID));
    }

    [[nodiscard]] std::vector<RE::FormID> UsedEffectSources(const Core::ActorKey a_actor, const Core::Target a_target) {
        std::vector<RE::FormID> used;
        const auto assignments = Equipment::AssignmentStore::GetSnapshot(a_actor);
        const auto* actorState = FindActorState(a_actor);
        for (const auto target : Core::kVirtualTargets) {
            if (target == a_target) {
                continue;
            }
            const auto& assignment = assignments.byTarget[Core::ToIndex(target)];
            if (!assignment.IsAssigned()) {
                continue;
            }
            if (assignment.retainedEffectSourceFormID != 0) {
                used.push_back(assignment.retainedEffectSourceFormID);
            }
            if (actorState) {
                const auto& state = actorState->targets[Core::ToIndex(target)];
                if (state.effectSource && state.activeAssignment.source == assignment.source) {
                    used.push_back(state.effectSource->GetFormID());
                }
            }
        }
        if (const auto pending = Equipment::RaceSwitchRestore::GetPendingRestore(a_actor)) {
            for (const auto& assignment : pending->assignments.byTarget) {
                if (assignment.retainedEffectSourceFormID != 0) {
                    used.push_back(assignment.retainedEffectSourceFormID);
                }
            }
        }
        return used;
    }

    [[nodiscard]] bool EnsureEffectSource(
        RE::Actor& a_actor,
        RE::TESObjectARMO& a_source,
        const ExtraRingMode a_mode,
        const bool a_requireRetainedEffectSource,
        TargetState& a_state,
        TargetRefreshPlan& a_plan
    ) {
        if (a_state.effectSource
            && EffectSources::Matches(a_state.effectSource->GetFormID(), a_plan.expectedAssignment.source, a_mode)) {
            a_plan.retainedEffectSourceFormID = a_state.effectSource->GetFormID();
            a_plan.updateRestoredEffectSource = true;
            return true;
        }

        const auto actor = Core::MakeActorKey(a_actor);
        MergeClearAction(a_plan, ExtractClearAction(actor, std::addressof(a_actor), a_state, true));
        auto* effectSource = EffectSources::Acquire(
            a_source,
            a_plan.expectedAssignment.source,
            a_mode,
            UsedEffectSources(actor, a_plan.target),
            a_plan.expectedAssignment.retainedEffectSourceFormID
        );
        if (!effectSource) {
            return false;
        }
        if (a_requireRetainedEffectSource
            && effectSource->GetFormID()
            != a_plan.expectedAssignment.retainedEffectSourceFormID) {
            SKSE::log::error(
                "Cannot preserve loaded ring effects for actor {:08X}, target {}",
                a_actor.GetFormID(),
                Core::TargetName(a_plan.target)
            );
            return false;
        }
        a_state.sourceFormID = a_source.GetFormID();
        a_state.effectSource = effectSource;
        a_plan.retainedEffectSourceFormID = effectSource->GetFormID();
        a_plan.updateRestoredEffectSource = true;
        return true;
    }
    [[nodiscard]] bool PrepareEnchantment(
        RE::Actor& a_actor,
        RE::TESObjectARMO const& a_ring,
        const Core::ItemSource& a_source,
        const ExtraRingMode a_mode,
        TargetState& a_state
    ) {
        RE::EnchantmentItem* enchantment = nullptr;
        if (a_source.IsCustomEnchantment()) {
            const auto matches = Inventory::FindCustomSourceMatches(
                a_actor,
                a_ring,
                a_source.customEnchantment,
                a_source.extraUniqueID
            );
            if (!matches.HasMatch()) {
                return false;
            }
            // Matching verifies the item's enchantment and identity without retaining its extra-data list.
            enchantment = matches.firstExtraList->GetByType<RE::ExtraEnchantment>()->enchantment;
        }
        a_state.customEnchantment = a_mode == ExtraRingMode::kFunctional ? enchantment : nullptr;
        return true;
    }

    [[nodiscard]] bool ReconcileRestoredEffectSource(
        RE::Actor& a_actor,
        RE::TESObjectARMO const& a_ring,
        const ExtraRingMode a_mode,
        const TargetState& a_state,
        TargetRefreshPlan& a_plan
    ) {
        const auto restoredEffectSource = a_state.effectSource
                                          != nullptr
                                          && a_state.effectSource->GetFormID()
                                          == a_plan.expectedAssignment.retainedEffectSourceFormID;
        const auto hasRestoredLoadedBinding = a_mode
                                              == ExtraRingMode::kFunctional
                                              && !a_state.active
                                              && restoredEffectSource
                                              && Papyrus::ScriptEventMirror::HasLoadedActiveBinding(
                                                  a_plan.actor,
                                                  a_ring.GetFormID(),
                                                  a_state.effectSource->GetFormID()
                                              );
        if (a_mode == ExtraRingMode::kCosmetic && !a_state.active) {
            const auto retainedSourceFormID = a_plan.expectedAssignment.retainedEffectSourceFormID;
            auto* retainedSource = RE::TESForm::LookupByID<RE::TESObjectARMO>(retainedSourceFormID);
            MergeClearAction(
                a_plan,
                ClearAction {
                    .actorKey = a_plan.actor,
                    .actor = std::addressof(a_actor),
                    .effectSource = retainedSource,
                    .sourceFormID = a_ring.GetFormID(),
                    .effectSourceFormID = retainedSourceFormID,
                    .dispatchUnequipped = true,
                    .active = true,
                }
            );
        }
        return hasRestoredLoadedBinding;
    }

    [[nodiscard]] bool ActivateTargetForRefresh(
        RE::Actor& a_actor,
        RE::TESObjectARMO const& a_ring,
        const ExtraRingMode a_mode,
        TargetState& a_state,
        TargetRefreshPlan& a_plan
    ) {
        const auto hasRestoredLoadedBinding = ReconcileRestoredEffectSource(a_actor, a_ring, a_mode, a_state, a_plan);

        if (!PrepareEnchantment(a_actor, a_ring, a_plan.expectedAssignment.source, a_mode, a_state)) {
            SKSE::log::warn(
                "VirtualSlots: effect source apply failed | target={} | source={:08X}",
                Core::TargetName(a_plan.target),
                a_ring.GetFormID()
            );
            a_plan.clearAssignment = true;
            auto clear = ExtractClearAction(a_plan.actor, std::addressof(a_actor), a_state, true);
            clear.active = clear.active || hasRestoredLoadedBinding;
            MergeClearAction(a_plan, clear);
            return false;
        }

        a_state.activeAssignment = a_plan.expectedAssignment;
        a_state.mode = a_mode;
        a_state.active = true;
        a_state.sourceTargets = SourceModelFootprints::GetRingGeometrySourceTargets(a_ring);
        return true;
    }

    [[nodiscard]] TargetRefreshPlan BuildTargetRefreshPlan( // NOLINT(readability-function-size)
        const Core::ActorKey a_actorKey,
        RE::Actor& a_actor,
        const Core::Target a_target,
        const RefreshState& a_refreshState
    ) {
        const auto assignment = Equipment::AssignmentStore::Get(a_actorKey, a_target);
        auto* ring = LookupItemSource(assignment);
        TargetRefreshPlan plan {
            .actor = a_actorKey,
            .target = a_target,
            .expectedAssignment = assignment,
            .retainedEffectSourceFormID = 0,
            .clearAssignment = false,
            .updateRestoredEffectSource = false,
        };

        std::scoped_lock const lock(g_lock);
        auto& actorState = GetOrCreateActorState(a_actorKey);
        auto& state = actorState.targets[Core::ToIndex(a_target)];
        const auto sound = ConsumePendingSound(actorState, a_target);
        const auto mode = Settings::GetSingleton()->GetExtraRingMode();
        const auto wasActive = state.active;

        if (!ring || !assignment.IsAssigned()) {
            const auto clearSound = sound == Audio::EquipSounds::Cue::kUnequip ? sound : Audio::EquipSounds::Cue::kNone;
            MergeClearAction(plan, ExtractClearAction(a_actorKey, std::addressof(a_actor), state, true, clearSound));
            return plan;
        }

        if (state.active && state.mode != mode) {
            MergeClearAction(plan, ExtractClearAction(a_actorKey, std::addressof(a_actor), state, true));
        }

        const auto sourceChanged = state.sourceFormID != ring->GetFormID();
        const auto assignmentChanged = state.activeAssignment.source != assignment.source;
        const auto changedAssignment = !state.active || sourceChanged || assignmentChanged || state.mode != mode;
        const auto preserveLoadedEffect = a_refreshState.preserveLoadedEffects
                                          && !wasActive
                                          && assignment.retainedEffectSourceFormID
                                          != 0
                                          && mode
                                          == ExtraRingMode::kFunctional;
        const auto preserveMatchingSource = preserveLoadedEffect
                                            && EffectSources::Matches(
                                                assignment.retainedEffectSourceFormID,
                                                assignment.source,
                                                mode
                                            );
        if (!EnsureEffectSource(a_actor, *ring, mode, preserveMatchingSource, state, plan)) {
            if (!preserveLoadedEffect) {
                SKSE::log::warn(
                    "VirtualSlots: effect source prepare failed | target={} | source={:08X}",
                    Core::TargetName(a_target),
                    ring->GetFormID()
                );
            }
            plan.clearAssignment = true;
            MergeClearAction(plan, ExtractClearAction(a_actorKey, std::addressof(a_actor), state, true));
            return plan;
        }

        if (!ActivateTargetForRefresh(a_actor, *ring, mode, state, plan)) {
            return plan;
        }

        if (mode == ExtraRingMode::kFunctional && changedAssignment && !preserveMatchingSource) {
            plan.apply = MakeApplyAction(
                a_actor,
                state,
                ring->GetFormID(),
                sound == Audio::EquipSounds::Cue::kEquip ? sound : Audio::EquipSounds::Cue::kNone,
                true
            );
        } else if (
            mode
            == ExtraRingMode::kFunctional
            && a_refreshState.restoreMissingEffects
            && !EnchantmentEffects::HasSourceEffects(a_actor, *state.effectSource)
        ) {
            // Cell unloading clears NPC enchantments while their virtual assignments remain.
            plan.apply = MakeApplyAction(a_actor, state, ring->GetFormID());
        }
        return plan;
    }

    void AddMagnitudeEffectReapplyActions(
        const Core::ActorKey a_actorKey,
        RE::Actor& a_actor,
        std::vector<TargetRefreshPlan>& a_plans
    ) {
        std::scoped_lock const lock(g_lock);
        const auto* actorState = FindActorState(a_actorKey);
        if (!actorState) {
            return;
        }

        for (auto& plan : a_plans) {
            if (HasApplyAction(plan.apply) || plan.clearAssignment) {
                continue;
            }

            const auto& state = actorState->targets[Core::ToIndex(plan.target)];
            if (!HasCountedMagnitudeEnchantment(state)) {
                continue;
            }

            plan.apply = MakeApplyAction(a_actor, state, state.sourceFormID);
        }
    }

    [[nodiscard]] RefreshState BeginRefresh(const Core::ActorKey a_actor) {
        std::scoped_lock const lock(g_lock);
        auto& actorState = GetOrCreateActorState(a_actor);
        actorState.refreshPending = false;

        const RefreshState state {
            .appliedMagnitudeRingCount = actorState.appliedMagnitudeRingCount,
            .preserveLoadedEffects = actorState.preserveLoadedEffects,
            .reapplyEffects = actorState.reapplyEffects,
            .restoreMissingEffects = actorState.restoreMissingEffects,
        };
        actorState.preserveLoadedEffects = false;
        actorState.reapplyEffects = false;
        actorState.restoreMissingEffects = false;
        return state;
    }

    void StoreAppliedMagnitudeRingCount(const Core::ActorKey a_actor, const std::uint32_t a_count) {
        std::scoped_lock const lock(g_lock);
        auto& actorState = GetOrCreateActorState(a_actor);
        actorState.appliedMagnitudeRingCount = a_count;
    }

    [[nodiscard]] std::vector<Visuals::Attachments::AttachmentSource> SnapshotAttachmentSources(
        const Core::ActorKey a_actor
    ) {
        std::vector<Visuals::Attachments::AttachmentSource> sources;
        if (!a_actor) {
            return sources;
        }

        std::scoped_lock const lock(g_lock);
        const auto* actorState = FindActorState(a_actor);
        if (!actorState) {
            return sources;
        }

        for (const auto target : Core::kVirtualTargets) {
            const auto& state = actorState->targets[Core::ToIndex(target)];
            if (!state.active || state.sourceFormID == 0) {
                continue;
            }

            sources.push_back(
                Visuals::Attachments::AttachmentSource {
                    .target = target,
                    .sourceTargets = state.sourceTargets,
                    .sourceFormID = state.sourceFormID,
                }
            );
        }

        return sources;
    }

    void RefreshOnce(const Core::ActorKey a_actor, const RefreshState& a_refreshState) {
        auto* actor = Core::ResolveActor(a_actor);
        if (!actor) {
            return;
        }

        const auto previousMagnitudeRingCount = CountEquippedMagnitudeRings(*actor);

        std::vector<TargetRefreshPlan> plans;
        plans.reserve(Core::kVirtualTargets.size());

        for (const auto target : Core::kVirtualTargets) {
            plans.push_back(BuildTargetRefreshPlan(a_actor, *actor, target, a_refreshState));
        }

        const auto nextMagnitudeRingCount = CountEquippedMagnitudeRings(*actor);
        const auto baselineMagnitudeRingCount = a_refreshState.appliedMagnitudeRingCount.value_or(
            previousMagnitudeRingCount
        );
        const auto* settings = Settings::GetSingleton();
        const auto magnitudeScaleChanged = !a_refreshState.preserveLoadedEffects
                                           && settings->GetRingEnchantmentScale(baselineMagnitudeRingCount)
                                           != settings->GetRingEnchantmentScale(nextMagnitudeRingCount);
        const auto reapplyMagnitudeEffects = a_refreshState.reapplyEffects || magnitudeScaleChanged;
        if (reapplyMagnitudeEffects) {
            AddMagnitudeEffectReapplyActions(a_actor, *actor, plans);
        }
        StoreAppliedMagnitudeRingCount(a_actor, nextMagnitudeRingCount);

        for (auto const& plan : plans) {
            for (const auto& clear : plan.clears) {
                RunClearAction(clear);
            }
            if (plan.clearAssignment) {
                Equipment::AssignmentStore::Clear(plan.actor, plan.target);
            }
            if (plan.updateRestoredEffectSource && !plan.clearAssignment) {
                static_cast<void>(Equipment::AssignmentStore::TrySetRetainedEffectSourceFormID(
                    plan.actor,
                    plan.target,
                    plan.expectedAssignment,
                    plan.retainedEffectSourceFormID
                ));
            }
        }

        for (const auto& plan : plans) {
            RunApplyAction(plan.apply);
        }

        if (reapplyMagnitudeEffects) {
            ReapplyVanillaRingSlotEffects(*actor);
        }
        RefreshVanillaCompatibility(a_actor);

        RequestVisualRefresh(a_actor);
    }

    void RunRefreshLoop(const Core::ActorKey a_actor) {
        if (!a_actor) {
            return;
        }

        for (;;) {
            const auto refreshState = BeginRefresh(a_actor);
            RefreshOnce(a_actor, refreshState);

            std::scoped_lock const lock(g_lock);
            auto* actorState = FindActorState(a_actor);
            if (!actorState) {
                return;
            }

            if (!actorState->refreshPending) {
                actorState->refreshRunning = false;
                return;
            }
        }
    }
}

void RequestRefresh(const Core::ActorKey a_actor, const RefreshOptions a_options) {
    if (!a_actor) {
        return;
    }

    bool shouldQueue = false;
    {
        std::scoped_lock const lock(g_lock);
        auto& actorState = GetOrCreateActorState(a_actor);
        StoreRefreshOptions(actorState, a_options);
        if (actorState.refreshPending) {
            return;
        }

        actorState.refreshPending = true;
        shouldQueue = !actorState.refreshRunning;
        if (shouldQueue) {
            actorState.refreshRunning = true;
        }
    }

    if (!shouldQueue) {
        return;
    }

    SKSE::GetTaskInterface()->AddTask([a_actor] { RunRefreshLoop(a_actor); });
}

void RequestVisualRefresh(const Core::ActorKey a_actor) {
    if (!a_actor) {
        return;
    }

    Visuals::Attachments::RequestRefresh(a_actor, SnapshotAttachmentSources(a_actor));
}

void ClearTarget(
    const Core::ActorKey a_actor,
    const Core::Target a_target,
    const Audio::EquipSounds::Cue a_sound,
    const ScriptBindingClearMode a_scriptBindings
) {
    if (!a_actor || !Core::IsVirtualTarget(a_target)) {
        return;
    }

    auto* actor = Core::ResolveActor(a_actor);
    ClearAction action;
    {
        std::scoped_lock const lock(g_lock);
        auto* actorState = FindActorState(a_actor);
        if (!actorState) {
            return;
        }

        actorState->pendingSounds[Core::ToIndex(a_target)] = Audio::EquipSounds::Cue::kNone;
        action = ExtractClearAction(
            a_actor,
            actor,
            actorState->targets[Core::ToIndex(a_target)],
            true,
            a_sound,
            a_scriptBindings
        );
    }
    RunClearAction(action);
    RefreshVanillaCompatibility(a_actor);
    RequestRefresh(a_actor, {});
    RequestVisualRefresh(a_actor);
}

void Revert() {
    std::vector<ClearAction> actions;
    {
        std::scoped_lock const lock(g_lock);
        auto& actorStates = ActorStates();
        actions.reserve(actorStates.size() * Core::kVirtualTargets.size());
        for (auto& [actorKey, actorState] : actorStates) {
            auto* actor = Core::ResolveActor(actorKey);
            for (auto& state : actorState.targets) {
                actions.push_back(ExtractClearAction(actorKey, actor, state, false));
            }
        }
        actorStates.clear();
    }
    for (const auto& action : actions) {
        RunClearAction(action);
    }
    EffectSources::Revert();
    Compatibility::Vanilla::Revert();
    Visuals::Attachments::Revert();
}

bool MatchesGetEquippedCondition(RE::Actor const& a_actor, RE::TESForm& a_getEquippedArgument) {
    const auto sourceFormIDs = SnapshotFunctionalVirtualSourceFormIDs(Core::MakeActorKey(a_actor));
    return std::ranges::any_of(sourceFormIDs, [&a_getEquippedArgument](const RE::FormID a_sourceFormID) {
        return SourceMatchesGetEquippedArgument(a_sourceFormID, a_getEquippedArgument);
    });
}

bool MatchesWornHasKeywordCondition(RE::Actor const& a_actor, RE::BGSKeyword& a_wornHasKeywordArgument) {
    const auto sourceFormIDs = SnapshotFunctionalVirtualSourceFormIDs(Core::MakeActorKey(a_actor));
    return std::ranges::any_of(sourceFormIDs, [&a_wornHasKeywordArgument](const RE::FormID a_sourceFormID) {
        return SourceMatchesWornHasKeywordArgument(a_sourceFormID, a_wornHasKeywordArgument);
    });
}

float GetRingEnchantmentScaleForSource(RE::Actor& a_actor, const RE::TESObjectARMO* a_source) {
    if (!SourceHasCountedMagnitudeEnchantment(a_actor, a_source)) {
        return 1.0F;
    }

    const auto count = CountEquippedMagnitudeRings(a_actor);
    const auto scale = Settings::GetSingleton()->GetRingEnchantmentScale(count);
    return std::clamp(scale, 0.0F, 1.0F);
}

std::vector<Papyrus::ScriptEventMirror::BindingRetentionKey> GetActiveBindingRetentionKeys() {
    std::vector<Papyrus::ScriptEventMirror::BindingRetentionKey> keys;
    std::scoped_lock const lock(g_lock);
    for (const auto& [actor, actorState] : ActorStates()) {
        for (const auto target : Core::kVirtualTargets) {
            const auto& state = actorState.targets[Core::ToIndex(target)];
            const auto functional = state.mode == ExtraRingMode::kFunctional;
            if (!state.active || !functional || state.sourceFormID == 0 || !state.effectSource) {
                continue;
            }

            keys.push_back(
                Papyrus::ScriptEventMirror::BindingRetentionKey {
                    .actor = actor,
                    .sourceFormID = state.sourceFormID,
                    .effectSourceFormID = state.effectSource->GetFormID(),
                }
            );
        }
    }

    return keys;
}
}
