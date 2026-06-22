#include "VirtualSlots.h"

#include "Audio/EquipSounds.h"
#include "Compatibility/Vanilla.h"
#include "Equipment/AssignmentStore.h"
#include "Inventory.h"
#include "Settings.h"
#include "SourceModelFootprints.h"
#include "VirtualSlots/EnchantmentEffects.h"
#include "Visuals/Attachments.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <unordered_map>
#include <utility>

namespace VirtualSlots {
namespace {
    struct CustomItemSource {
        RE::ExtraDataList* extraList {nullptr};
        Inventory::CustomEnchantmentData customData;
    };

    struct ExtraListDeleter {
        void operator()(RE::ExtraDataList* a_extraList) const {
            if (!a_extraList) {
                return;
            }

            a_extraList->RemoveByType(RE::ExtraDataType::kEnchantment);
            a_extraList->RemoveByType(RE::ExtraDataType::kTextDisplayData);
            RE::free(a_extraList);
        }
    };

    using ExtraDataListPtr = std::shared_ptr<RE::ExtraDataList>;

    struct TargetState {
        RE::FormID sourceFormID {0};
        RE::TESObjectARMO* effectSource {nullptr};
        ExtraDataListPtr extraList;
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
    };

    struct ClearAction {
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
        ExtraDataListPtr extraList;
        RE::FormID sourceFormID {0};
        Audio::EquipSounds::Cue sound {Audio::EquipSounds::Cue::kNone};
        bool dispatchEquipped {false};
    };

    struct RefreshState {
        std::optional<std::uint32_t> appliedMagnitudeRingCount;
        bool preserveLoadedEffects {false};
        bool reapplyEffects {false};
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
        if (const auto it = states.find(a_actor); it != states.end()) {
            return std::addressof(it->second);
        }

        return nullptr;
    }

    [[nodiscard]] bool HasApplyAction(const ApplyAction& a_action) {
        return a_action.actor && a_action.effectSource;
    }

    [[nodiscard]] bool SourceMatchesGetEquippedArgument(
        const RE::FormID a_sourceFormID,
        RE::TESForm& a_getEquippedArgument
    ) {
        if (a_sourceFormID == 0) {
            return false;
        }

        if (auto* list = a_getEquippedArgument.As<RE::BGSListForm>()) {
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

        auto* source = Inventory::AsRing(RE::TESForm::LookupByID(a_sourceFormID));
        return source && source->HasKeyword(std::addressof(a_wornHasKeywordArgument));
    }

    [[nodiscard]] ExtraDataListPtr CreateExtraDataList() {
        constexpr auto kExtraDataListAllocationSize = std::size_t {0x20};
        auto* extraList = RE::calloc<RE::ExtraDataList>(1, kExtraDataListAllocationSize);
        if (!extraList) {
            return nullptr;
        }

        return ExtraDataListPtr {extraList, ExtraListDeleter {}};
    }

    template <class T>
    [[nodiscard]] T* DuplicateForm(T& a_source, std::string_view a_kind) {
        auto* duplicateForm = a_source.CreateDuplicateForm(true, nullptr);
        auto* duplicate = duplicateForm ? duplicateForm->template As<T>() : nullptr;
        if (!duplicate) {
            logger::warn("VirtualSlots: duplicate failed | kind={} | source={:08X}", a_kind, a_source.GetFormID());
            return nullptr;
        }

        return duplicate;
    }

    [[nodiscard]] bool RegisterForm(
        RE::TESForm& a_form,
        const std::string_view a_kind,
        const RE::FormID a_sourceFormID
    ) {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            logger::warn(
                "VirtualSlots: register failed | kind={} | source={:08X} | reason=noDataHandler",
                a_kind,
                a_sourceFormID
            );
            return false;
        }

        if (!dataHandler->AddFormToDataHandler(std::addressof(a_form))) {
            logger::warn(
                "VirtualSlots: register failed | kind={} | source={:08X} | effectSource={:08X} | reason=dataHandlerRejected",
                a_kind,
                a_sourceFormID,
                a_form.GetFormID()
            );
            return false;
        }

        return true;
    }

    void ConfigureEffectSource(const RE::TESObjectARMO& a_source, RE::TESObjectARMO& a_effectSource) {
        a_effectSource.SetSlotMask(RE::BGSBipedObjectForm::BipedObjectSlot::kNone);
        a_effectSource.armorAddons.clear();
        a_effectSource.formFlags |= RE::TESObjectARMO::RecordFlags::kNonPlayable;
        a_effectSource.value = 0;
        a_effectSource.weight = 0.0F;
        a_effectSource.SetFullName(a_source.GetName() ? a_source.GetName() : "");
    }

    [[nodiscard]] bool HasCountedMagnitudeEnchantment(const TargetState& a_state) {
        const auto functional = a_state.mode == ExtraRingMode::kFunctional;
        return a_state.active
               && functional
               && a_state.effectSource
               && VirtualSlots::EnchantmentEffects::HasMagnitudeEnchantment(
                   *a_state.effectSource,
                   a_state.extraList.get()
               );
    }

    [[nodiscard]] bool HasEffectSource(const TargetState& a_state, const RE::TESObjectARMO* a_armor) {
        return a_armor && a_state.effectSource == a_armor;
    }

    [[nodiscard]] bool IsCountedVirtualEffectSource(const Core::ActorKey a_actor, const RE::TESObjectARMO* a_armor) {
        if (!a_actor || !a_armor) {
            return false;
        }

        std::scoped_lock lock(g_lock);
        const auto* actorState = FindActorState(a_actor);
        if (!actorState) {
            return false;
        }

        return std::ranges::any_of(Core::kVirtualTargets, [actorState, a_armor](const auto target) {
            const auto& state = actorState->targets[Core::ToIndex(target)];
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

        std::scoped_lock lock(g_lock);
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

        auto existing = std::ranges::find_if(a_plan.clears, [&](const auto& a_existing) {
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
            Papyrus::ScriptEventMirror::RemoveEffectSourceBindings(a_action.effectSourceFormID);
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
            a_action.extraList.get()
        );

        if (a_action.dispatchEquipped) {
            auto* sourceRing = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_action.sourceFormID);
            if (sourceRing) {
                static_cast<void>(
                    Papyrus::ScriptEventMirror::DispatchEquipped(*a_action.actor, *sourceRing, *a_action.effectSource)
                );
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
            .extraList = a_state.extraList,
            .sourceFormID = a_sourceFormID,
            .sound = a_sound,
            .dispatchEquipped = a_dispatchEquipped,
        };
    }

    [[nodiscard]] std::vector<RE::FormID> SnapshotFunctionalVirtualSourceFormIDs(const Core::ActorKey a_actor) {
        std::vector<RE::FormID> sourceFormIDs;
        if (!a_actor) {
            return sourceFormIDs;
        }

        std::scoped_lock lock(g_lock);
        const auto* actorState = FindActorState(a_actor);
        if (!actorState) {
            return sourceFormIDs;
        }

        for (const auto target : Core::kVirtualTargets) {
            const auto& state = actorState->targets[Core::ToIndex(target)];
            if (!state.active || state.mode != ExtraRingMode::kFunctional || state.sourceFormID == 0) {
                continue;
            }

            sourceFormIDs.push_back(state.sourceFormID);
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

    [[nodiscard]] std::optional<CustomItemSource> ResolveCustomItemSource(
        RE::Actor& a_actor,
        const Core::Assignment& a_assignment,
        bool& a_clearAssignment
    ) {
        auto* sourceRing = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_assignment.source.sourceFormID);
        if (!sourceRing) {
            a_clearAssignment = true;
            return std::nullopt;
        }

        const auto customEnchantment = a_assignment.source.customEnchantment;
        const auto sourceMatches = Inventory::FindCustomSourceMatches(
            a_actor,
            *sourceRing,
            customEnchantment,
            a_assignment.source.extraUniqueID
        );
        auto* sourceExtraList = sourceMatches.firstExtraList;
        if (!sourceExtraList
            || !Inventory::MatchesCustomSelection(
                sourceExtraList,
                customEnchantment,
                a_assignment.source.extraUniqueID
            )) {
            a_clearAssignment = true;
            return std::nullopt;
        }

        auto customData = Inventory::ReadCustomEnchantment(*sourceExtraList);
        if (!customData) {
            a_clearAssignment = true;
            return std::nullopt;
        }

        return CustomItemSource {
            .extraList = sourceExtraList,
            .customData = std::move(*customData),
        };
    }

    [[nodiscard]] bool EnsureEffectSource(
        RE::Actor& a_actor,
        const Core::Target a_target,
        RE::TESObjectARMO& a_source,
        const Core::Assignment& a_assignment,
        const bool a_requireRetainedEffectSource,
        TargetState& a_state,
        TargetRefreshPlan& a_plan
    ) {
        if (a_state.effectSource && a_state.sourceFormID == a_source.GetFormID()) {
            ConfigureEffectSource(a_source, *a_state.effectSource);
            a_plan.retainedEffectSourceFormID = a_state.effectSource->GetFormID();
            a_plan.updateRestoredEffectSource = true;
            return true;
        }

        MergeClearAction(a_plan, ExtractClearAction(std::addressof(a_actor), a_state, true));

        RE::TESObjectARMO* effectSource = nullptr;
        if (a_assignment.retainedEffectSourceFormID != 0) {
            effectSource = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_assignment.retainedEffectSourceFormID);
            if (!effectSource && !a_requireRetainedEffectSource) {
                logger::warn(
                    "VirtualSlots: restored effect source skipped | target={} | source={:08X} | effectSource={:08X} | reason=missing",
                    Core::TargetName(a_target),
                    a_source.GetFormID(),
                    a_assignment.retainedEffectSourceFormID
                );
            }
        }

        if (!effectSource && a_requireRetainedEffectSource) {
            logger::warn(
                "VirtualSlots: load-preserved assignment cleared | target={} | source={:08X} | effectSource={:08X} | reason=effectSourceMissing",
                Core::TargetName(a_target),
                a_source.GetFormID(),
                a_assignment.retainedEffectSourceFormID
            );
            return false;
        }

        if (!effectSource) {
            effectSource = DuplicateForm(a_source, "ARMO"sv);
            if (!effectSource) {
                return false;
            }

            ConfigureEffectSource(a_source, *effectSource);
            if (!RegisterForm(*effectSource, "ARMO"sv, a_source.GetFormID())) {
                return false;
            }
        } else {
            ConfigureEffectSource(a_source, *effectSource);
        }

        a_state.sourceFormID = a_source.GetFormID();
        a_state.effectSource = effectSource;
        a_plan.retainedEffectSourceFormID = effectSource->GetFormID();
        a_plan.updateRestoredEffectSource = true;
        return true;
    }

    [[nodiscard]] bool ApplyAssignmentToEffectSource(
        RE::Actor& a_actor,
        RE::TESObjectARMO& a_source,
        const Core::Assignment& a_assignment,
        const ExtraRingMode a_mode,
        TargetState& a_state,
        bool& a_clearAssignment
    ) {
        if (!a_state.effectSource) {
            return false;
        }

        a_state.effectSource->SetFullName(a_source.GetName() ? a_source.GetName() : "");

        if (a_mode == ExtraRingMode::kCosmetic) {
            const auto customAssignment = a_assignment.source.kind == Core::ItemSourceKind::kCustomEnchantment;
            if (customAssignment && !ResolveCustomItemSource(a_actor, a_assignment, a_clearAssignment)) {
                return false;
            }

            a_state.extraList.reset();
            a_state.effectSource->formEnchanting = nullptr;
            a_state.effectSource->amountofEnchantment = 0;
            return true;
        }

        if (a_assignment.source.kind == Core::ItemSourceKind::kCustomEnchantment) {
            auto customSource = ResolveCustomItemSource(a_actor, a_assignment, a_clearAssignment);
            if (!customSource) {
                return false;
            }

            a_state.effectSource->formEnchanting = nullptr;
            a_state.effectSource->amountofEnchantment = 0;
            a_state.extraList = CreateExtraDataList();
            if (!a_state.extraList) {
                return false;
            }
            if (!Inventory::MirrorCustomEnchantment(*a_state.extraList, *customSource->extraList)) {
                return false;
            }
            if (customSource->customData.playerDisplayName) {
                a_state.effectSource->SetFullName(customSource->customData.playerDisplayName->c_str());
            }
            return true;
        }

        a_state.extraList.reset();
        a_state.effectSource->formEnchanting = a_source.formEnchanting;
        a_state.effectSource->amountofEnchantment = a_source.amountofEnchantment;
        return true;
    }

    [[nodiscard]] TargetRefreshPlan BuildTargetRefreshPlan(
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
        };

        std::scoped_lock lock(g_lock);
        auto& actorState = GetOrCreateActorState(a_actorKey);
        auto& state = actorState.targets[Core::ToIndex(a_target)];
        const auto sound = ConsumePendingSound(actorState, a_target);
        const auto mode = Settings::GetSingleton()->GetExtraRingMode();
        const auto wasActive = state.active;

        if (!ring || !assignment.IsAssigned()) {
            const auto clearSound = sound == Audio::EquipSounds::Cue::kUnequip ? sound : Audio::EquipSounds::Cue::kNone;
            MergeClearAction(plan, ExtractClearAction(std::addressof(a_actor), state, true, clearSound));
            return plan;
        }

        const auto modeChanged = state.active && state.mode != mode;
        if (modeChanged) {
            MergeClearAction(plan, ExtractClearAction(std::addressof(a_actor), state, true));
        }

        const auto sourceChanged = state.sourceFormID != ring->GetFormID();
        const auto assignmentChanged = state.activeAssignment.source != assignment.source;
        const auto changedAssignment = !state.active || sourceChanged || assignmentChanged || state.mode != mode;
        const auto hasRetainedEffectSource = assignment.retainedEffectSourceFormID != 0;
        const auto preserveLoadedEffect = a_refreshState.preserveLoadedEffects
                                          && !wasActive
                                          && hasRetainedEffectSource
                                          && mode
                                          == ExtraRingMode::kFunctional;

        if (!EnsureEffectSource(a_actor, a_target, *ring, assignment, preserveLoadedEffect, state, plan)) {
            if (!preserveLoadedEffect) {
                logger::warn(
                    "VirtualSlots: effect source prepare failed | target={} | source={:08X}",
                    Core::TargetName(a_target),
                    ring->GetFormID()
                );
            }
            plan.clearAssignment = true;
            MergeClearAction(plan, ExtractClearAction(std::addressof(a_actor), state, true));
            return plan;
        }

        const auto restoredEffectSource = state.effectSource
                                          && state.effectSource->GetFormID()
                                          == assignment.retainedEffectSourceFormID;
        const auto hasRestoredLoadedBinding = mode
                                              == ExtraRingMode::kFunctional
                                              && !state.active
                                              && restoredEffectSource
                                              && Papyrus::ScriptEventMirror::HasLoadedActiveBinding(
                                                  ring->GetFormID(),
                                                  state.effectSource->GetFormID()
                                              );
        if (mode == ExtraRingMode::kCosmetic && !state.active && restoredEffectSource) {
            MergeClearAction(
                plan,
                ClearAction {
                    .actor = std::addressof(a_actor),
                    .effectSource = state.effectSource,
                    .sourceFormID = state.sourceFormID,
                    .effectSourceFormID = state.effectSource->GetFormID(),
                    .dispatchUnequipped = true,
                    .active = true,
                }
            );
        }

        bool clearAssignment = false;
        if (!ApplyAssignmentToEffectSource(a_actor, *ring, assignment, mode, state, clearAssignment)) {
            logger::warn(
                "VirtualSlots: effect source apply failed | target={} | source={:08X}",
                Core::TargetName(a_target),
                ring->GetFormID()
            );
            plan.clearAssignment = clearAssignment;
            auto clear = ExtractClearAction(std::addressof(a_actor), state, true);
            clear.active = clear.active || hasRestoredLoadedBinding;
            MergeClearAction(plan, clear);
            return plan;
        }

        state.activeAssignment = assignment;
        state.mode = mode;
        state.active = true;
        state.sourceTargets = SourceModelFootprints::GetRingGeometrySourceTargets(*ring);

        if (mode == ExtraRingMode::kFunctional && changedAssignment && !preserveLoadedEffect) {
            plan.apply = MakeApplyAction(
                a_actor,
                state,
                ring->GetFormID(),
                sound == Audio::EquipSounds::Cue::kEquip ? sound : Audio::EquipSounds::Cue::kNone,
                true
            );
        }
        return plan;
    }

    void AddMagnitudeEffectReapplyActions(
        const Core::ActorKey a_actorKey,
        RE::Actor& a_actor,
        std::vector<TargetRefreshPlan>& a_plans
    ) {
        std::scoped_lock lock(g_lock);
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
        std::scoped_lock lock(g_lock);
        auto& actorState = GetOrCreateActorState(a_actor);
        actorState.refreshPending = false;

        const RefreshState state {
            .appliedMagnitudeRingCount = actorState.appliedMagnitudeRingCount,
            .preserveLoadedEffects = actorState.preserveLoadedEffects,
            .reapplyEffects = actorState.reapplyEffects,
        };
        actorState.preserveLoadedEffects = false;
        actorState.reapplyEffects = false;
        return state;
    }

    void StoreAppliedMagnitudeRingCount(const Core::ActorKey a_actor, const std::uint32_t a_count) {
        std::scoped_lock lock(g_lock);
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

        std::scoped_lock lock(g_lock);
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
        const auto magnitudeCountChanged = !a_refreshState.preserveLoadedEffects
                                           && baselineMagnitudeRingCount
                                           != nextMagnitudeRingCount;
        const auto reapplyMagnitudeEffects = a_refreshState.reapplyEffects || magnitudeCountChanged;
        if (reapplyMagnitudeEffects) {
            AddMagnitudeEffectReapplyActions(a_actor, *actor, plans);
        }
        StoreAppliedMagnitudeRingCount(a_actor, nextMagnitudeRingCount);

        for (auto& plan : plans) {
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

            std::scoped_lock lock(g_lock);
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
        std::scoped_lock lock(g_lock);
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

    stl::add_task([a_actor] {
        RunRefreshLoop(a_actor);
    });
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
        std::scoped_lock lock(g_lock);
        auto* actorState = FindActorState(a_actor);
        if (!actorState) {
            return;
        }

        actorState->pendingSounds[Core::ToIndex(a_target)] = Audio::EquipSounds::Cue::kNone;
        action = ExtractClearAction(
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
        std::scoped_lock lock(g_lock);
        auto& actorStates = ActorStates();
        actions.reserve(actorStates.size() * Core::kVirtualTargets.size());
        for (auto& [actorKey, actorState] : actorStates) {
            auto* actor = Core::ResolveActor(actorKey);
            for (auto& state : actorState.targets) {
                actions.push_back(ExtractClearAction(actor, state, false));
            }
        }
        actorStates.clear();
    }
    for (const auto& action : actions) {
        RunClearAction(action);
    }
    Compatibility::Vanilla::Revert();
    Visuals::Attachments::Revert();
}

bool MatchesGetEquippedCondition(RE::Actor& a_actor, RE::TESForm& a_getEquippedArgument) {
    const auto sourceFormIDs = SnapshotFunctionalVirtualSourceFormIDs(Core::MakeActorKey(a_actor));
    return std::ranges::any_of(sourceFormIDs, [&a_getEquippedArgument](const RE::FormID a_sourceFormID) {
        return SourceMatchesGetEquippedArgument(a_sourceFormID, a_getEquippedArgument);
    });
}

bool MatchesWornHasKeywordCondition(RE::Actor& a_actor, RE::BGSKeyword& a_wornHasKeywordArgument) {
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
    std::scoped_lock lock(g_lock);
    for (const auto& actorState : ActorStates() | std::views::values) {
        for (const auto target : Core::kVirtualTargets) {
            const auto& state = actorState.targets[Core::ToIndex(target)];
            const auto functional = state.mode == ExtraRingMode::kFunctional;
            if (!state.active || !functional || state.sourceFormID == 0 || !state.effectSource) {
                continue;
            }

            keys.push_back(
                Papyrus::ScriptEventMirror::BindingRetentionKey {
                    .sourceFormID = state.sourceFormID,
                    .effectSourceFormID = state.effectSource->GetFormID(),
                }
            );
        }
    }

    return keys;
}
}
