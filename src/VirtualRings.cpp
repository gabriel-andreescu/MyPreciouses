#include "VirtualRings.h"

#include "Inventory.h"
#include "RingEnchantments.h"
#include "RingFootprints.h"
#include "RingSounds.h"
#include "RingVisuals.h"
#include "Selection.h"
#include "Settings.h"
#include "VanillaCompatibility.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <utility>

namespace VirtualRings {
namespace {
    constexpr std::array<RE::SEX, RE::SEXES::kTotal> kSexes {RE::SEXES::kMale, RE::SEXES::kFemale};

    struct CustomSelectionSource {
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
        Selection::State activeSelection;
        ExtraRingMode mode {ExtraRingMode::kFunctional};
        RingFootprints::RingFootprint footprint;
        std::vector<SourceAddonVisual> addonVisuals;
        bool active {false};
    };

    struct PendingClear {
        RE::Actor* actor {nullptr};
        RE::TESObjectARMO* effectSource {nullptr};
        RE::FormID sourceFormID {0};
        RE::FormID effectSourceFormID {0};
        RingSounds::Event sound {RingSounds::Event::kNone};
        bool dispatchUnequipped {false};
        bool active {false};
    };

    struct PendingApply {
        RE::Actor* actor {nullptr};
        RE::TESObjectARMO* effectSource {nullptr};
        ExtraDataListPtr extraList;
        RE::FormID sourceFormID {0};
        RingSounds::Event sound {RingSounds::Event::kNone};
        bool dispatchEquipped {false};
    };

    struct RefreshPlan {
        RingTarget target {kDefaultLeftEquipTarget};
        std::vector<PendingClear> clears;
        PendingApply apply;
        RE::FormID restoredEffectSourceFormID {0};
        bool clearSelection {false};
        bool updateRestoredEffectSource {false};
    };

    [[nodiscard]] std::array<TargetState, kRingTargets.size()>& TargetStates() {
        static auto* states = new std::array<TargetState, kRingTargets.size()>();
        return *states;
    }

    std::mutex g_lock;
    std::array<RingSounds::Event, kRingTargets.size()> g_pendingSounds {};
    bool g_refreshPending {false};
    bool g_refreshRunning {false};

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

    [[nodiscard]] const char* GetModel(const RE::TESModel& a_model) {
        const auto* model = a_model.GetModel();
        return model && model[0] != '\0' ? model : nullptr;
    }

    [[nodiscard]] ExtraDataListPtr CreateExtraDataList() {
        constexpr auto kExtraDataListAllocationSize = std::size_t {0x20};
        auto* extraList = RE::calloc<RE::ExtraDataList>(1, kExtraDataListAllocationSize);
        if (!extraList) {
            return nullptr;
        }

        return ExtraDataListPtr {extraList, ExtraListDeleter {}};
    }

    [[nodiscard]] SourceAddonVisual::ModelVariant CaptureModelVariant(const RE::TESModelTextureSwap& a_model) {
        SourceAddonVisual::ModelVariant model;
        if (const auto* path = GetModel(a_model)) {
            model.path = path;
            model.textureSwap = std::addressof(a_model);
            model.alternateTextures = a_model.alternateTextures;
            model.numAlternateTextures = a_model.numAlternateTextures;
        }

        return model;
    }

    [[nodiscard]] std::optional<SourceAddonVisual> CaptureSourceAddonVisual(
        const RE::TESObjectARMA& a_sourceAddon,
        const RingTarget a_target,
        RE::TESRace& a_actorRace,
        const bool a_customRingSlots
    ) {
        if (!a_customRingSlots && !a_sourceAddon.HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kRing)) {
            return std::nullopt;
        }
        if (!a_sourceAddon.IsValidRace(std::addressof(a_actorRace))) {
            return std::nullopt;
        }

        SourceAddonVisual visual;
        visual.target = a_target;
        visual.sourceAddon = std::addressof(a_sourceAddon);
        bool foundAnyModel = false;

        for (const auto sex : kSexes) {
            const auto sexIndex = std::to_underlying(sex);
            visual.thirdPerson[sexIndex] = CaptureModelVariant(a_sourceAddon.bipedModels[sexIndex]);
            visual.firstPerson[sexIndex] = CaptureModelVariant(a_sourceAddon.bipedModel1stPersons[sexIndex]);

            foundAnyModel = foundAnyModel
                            || !visual.thirdPerson[sexIndex].path.empty()
                            || !visual.firstPerson[sexIndex].path.empty();
        }

        return foundAnyModel ? std::make_optional(std::move(visual)) : std::nullopt;
    }

    [[nodiscard]] std::vector<SourceAddonVisual> CaptureRingVisuals(
        const RE::TESObjectARMO& a_source,
        const RingTarget a_target,
        RE::TESRace& a_actorRace
    ) {
        std::vector<SourceAddonVisual> visuals;
        const auto customRingSlots = !a_source.HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kRing)
                                     || Inventory::HasClothingRingKeyword(std::addressof(a_source));

        for (auto* addon : a_source.armorAddons) {
            if (!addon) {
                continue;
            }

            if (auto visual = CaptureSourceAddonVisual(*addon, a_target, a_actorRace, customRingSlots)) {
                visuals.push_back(std::move(*visual));
            }
        }
        return visuals;
    }

    template <class T>
    [[nodiscard]] T* DuplicateForm(T& a_source, std::string_view a_kind) {
        auto* duplicateForm = a_source.CreateDuplicateForm(true, nullptr);
        auto* duplicate = duplicateForm ? duplicateForm->template As<T>() : nullptr;
        if (!duplicate) {
            logger::warn("VirtualRings: duplicate failed | kind={} | source={:08X}", a_kind, a_source.GetFormID());
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
                "VirtualRings: register failed | kind={} | source={:08X} | reason=noDataHandler",
                a_kind,
                a_sourceFormID
            );
            return false;
        }

        if (!dataHandler->AddFormToDataHandler(std::addressof(a_form))) {
            logger::warn(
                "VirtualRings: register failed | kind={} | source={:08X} | effectSource={:08X} | reason=dataHandlerRejected",
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

    [[nodiscard]] RE::VMHandle GetObjectHandle(const RE::TESForm& a_form) {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        auto* handlePolicy = vm ? vm->GetObjectHandlePolicy() : nullptr;
        if (!handlePolicy) {
            return 0;
        }

        return handlePolicy->GetHandleForObject(a_form.GetFormType(), std::addressof(a_form));
    }

    [[nodiscard]] bool CountsForEnchantmentStrength(const TargetState& a_state) {
        const auto functional = a_state.mode == ExtraRingMode::kFunctional;
        return a_state.active
               && functional
               && a_state.effectSource
               && RingEnchantments::HasMagnitudeEnchantment(*a_state.effectSource, a_state.extraList.get());
    }

    void DispatchVirtualEquipped(
        RE::Actor& a_actor,
        const RE::FormID a_sourceFormID,
        RE::TESObjectARMO& a_effectSource
    ) {
        auto* sourceRing = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_sourceFormID);
        if (!sourceRing) {
            return;
        }

        const auto effectSourceFormID = a_effectSource.GetFormID();
        if (EventBindings::HasLoadedBinding(sourceRing->GetFormID(), effectSourceFormID)
            && EventBindings::AdoptLoadedBinding(*sourceRing, effectSourceFormID)) {
            return;
        }

        const auto handle = GetObjectHandle(a_effectSource);
        if (handle == 0) {
            return;
        }

        static_cast<void>(EventBindings::MirrorScriptsAndDispatch(
            *sourceRing,
            effectSourceFormID,
            handle,
            a_actor,
            RE::BSFixedString {"OnEquipped"}
        ));
    }

    [[nodiscard]] PendingClear BuildClearPlan(
        RE::Actor* a_actor,
        TargetState& a_state,
        const bool a_dispatchUnequipped,
        const RingSounds::Event a_sound = RingSounds::Event::kNone
    ) {
        if (!a_state.effectSource) {
            a_state = {};
            return {};
        }

        const PendingClear action {
            .actor = a_actor,
            .effectSource = a_state.effectSource,
            .sourceFormID = a_state.sourceFormID,
            .effectSourceFormID = a_state.effectSource->GetFormID(),
            .sound = a_sound,
            .dispatchUnequipped = a_dispatchUnequipped,
            .active = a_state.active,
        };

        a_state = {};
        return action;
    }

    void AddPendingClear(RefreshPlan& a_plan, PendingClear a_clear) {
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
        if (existing->sound == RingSounds::Event::kNone) {
            existing->sound = a_clear.sound;
        }
        existing->dispatchUnequipped = existing->dispatchUnequipped || a_clear.dispatchUnequipped;
        existing->active = existing->active || a_clear.active;
    }

    void RunPendingClear(const PendingClear& a_action) {
        if (!a_action.effectSource || a_action.effectSourceFormID == 0) {
            return;
        }

        if (a_action.actor) {
            RingEnchantments::DispelEffectsFromSource(*a_action.actor, *a_action.effectSource);
        }

        if (a_action.dispatchUnequipped && a_action.actor && a_action.active) {
            EventBindings::RemoveForEffectSource(a_action.effectSourceFormID, *a_action.actor);
        } else {
            EventBindings::RemoveForEffectSource(a_action.effectSourceFormID);
        }

        if (a_action.actor && a_action.active) {
            RingSounds::Play(*a_action.actor, a_action.sourceFormID, a_action.sound);
        }
    }

    void RunPendingApply(const PendingApply& a_action) {
        if (!a_action.actor || !a_action.effectSource) {
            return;
        }

        RingEnchantments::DispelEffectsFromSource(*a_action.actor, *a_action.effectSource);
        RingEnchantments::ApplyVirtualRingEnchantment(
            *a_action.actor,
            *a_action.effectSource,
            a_action.extraList.get()
        );

        if (a_action.dispatchEquipped) {
            DispatchVirtualEquipped(*a_action.actor, a_action.sourceFormID, *a_action.effectSource);
        }

        RingSounds::Play(*a_action.actor, a_action.sourceFormID, a_action.sound);
    }

    [[nodiscard]] std::vector<RE::FormID> GetFunctionalVirtualRingSourceFormIDs() {
        std::vector<RE::FormID> sourceFormIDs;
        std::scoped_lock lock(g_lock);
        for (const auto target : kVirtualRingTargets) {
            const auto& state = TargetStates()[ToIndex(target)];
            if (!state.active || state.mode != ExtraRingMode::kFunctional || state.sourceFormID == 0) {
                continue;
            }

            sourceFormIDs.push_back(state.sourceFormID);
        }

        return sourceFormIDs;
    }

    void RefreshVanillaCompatibility() {
        const auto sourceFormIDs = GetFunctionalVirtualRingSourceFormIDs();
        VanillaCompatibility::RefreshFrostmoonVirtualRings(
            std::span<const RE::FormID> {sourceFormIDs.data(), sourceFormIDs.size()}
        );
    }

    void StorePendingSound(const RefreshOptions& a_options) {
        if (a_options.sound == RingSounds::Event::kNone || !a_options.soundTarget) {
            return;
        }

        if (!IsVirtualRingTarget(*a_options.soundTarget)) {
            return;
        }

        g_pendingSounds[ToIndex(*a_options.soundTarget)] = a_options.sound;
    }

    [[nodiscard]] RingSounds::Event ConsumePendingSound(const RingTarget a_target) {
        auto& sound = g_pendingSounds[ToIndex(a_target)];
        const auto result = sound;
        sound = RingSounds::Event::kNone;
        return result;
    }

    [[nodiscard]] std::optional<CustomSelectionSource> ResolveCustomSelectionSource(
        RE::Actor& a_actor,
        const Selection::State& a_selection,
        bool& a_clearSelection
    ) {
        auto* sourceRing = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_selection.sourceFormID);
        if (!sourceRing) {
            a_clearSelection = true;
            return std::nullopt;
        }

        const auto customKey = a_selection.GetCustomKey();
        const auto sourceMatches = Inventory::FindSourceMatches(
            a_actor,
            *sourceRing,
            customKey,
            a_selection.GetCustomIdentity()
        );
        auto* sourceExtraList = sourceMatches.firstExtraList;
        if (!sourceExtraList
            || !Inventory::MatchesCustomSelection(sourceExtraList, customKey, a_selection.GetCustomIdentity())) {
            a_clearSelection = true;
            return std::nullopt;
        }

        auto customData = Inventory::ReadCustomEnchantment(*sourceExtraList);
        if (!customData) {
            a_clearSelection = true;
            return std::nullopt;
        }

        return CustomSelectionSource {
            .extraList = sourceExtraList,
            .customData = std::move(*customData),
        };
    }

    [[nodiscard]] bool EnsureEffectSource(
        const RingTarget a_target,
        RE::TESObjectARMO& a_source,
        const Selection::State& a_selection,
        TargetState& a_state,
        RefreshPlan& a_plan
    ) {
        if (a_state.effectSource && a_state.sourceFormID == a_source.GetFormID()) {
            ConfigureEffectSource(a_source, *a_state.effectSource);
            return true;
        }

        AddPendingClear(a_plan, BuildClearPlan(RE::PlayerCharacter::GetSingleton(), a_state, true));

        RE::TESObjectARMO* effectSource = nullptr;
        if (a_selection.restoredEffectSourceFormID != 0) {
            effectSource = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_selection.restoredEffectSourceFormID);
            if (!effectSource) {
                logger::warn(
                    "VirtualRings: restored effect source skipped | target={} | source={:08X} | effectSource={:08X} | reason=missing",
                    TargetLabel(a_target),
                    a_source.GetFormID(),
                    a_selection.restoredEffectSourceFormID
                );
            }
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
        a_plan.restoredEffectSourceFormID = effectSource->GetFormID();
        a_plan.updateRestoredEffectSource = true;
        return true;
    }

    [[nodiscard]] bool ApplySelectionToEffectSource(
        RE::Actor& a_actor,
        RE::TESObjectARMO& a_source,
        const Selection::State& a_selection,
        const ExtraRingMode a_mode,
        TargetState& a_state,
        bool& a_clearSelection
    ) {
        if (!a_state.effectSource) {
            return false;
        }

        a_state.effectSource->SetFullName(a_source.GetName() ? a_source.GetName() : "");

        if (a_mode == ExtraRingMode::kCosmetic) {
            const auto customSelection = a_selection.kind == Selection::Kind::kCustomEnchantment;
            if (customSelection && !ResolveCustomSelectionSource(a_actor, a_selection, a_clearSelection)) {
                return false;
            }

            a_state.extraList.reset();
            a_state.effectSource->formEnchanting = nullptr;
            a_state.effectSource->amountofEnchantment = 0;
            return true;
        }

        if (a_selection.kind == Selection::Kind::kCustomEnchantment) {
            auto customSource = ResolveCustomSelectionSource(a_actor, a_selection, a_clearSelection);
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

    [[nodiscard]] RefreshPlan BuildRefreshPlan(const RingTarget a_target) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto selection = Selection::Get(a_target);
        auto* ring = Selection::GetSource(a_target);
        RefreshPlan plan {
            .target = a_target,
        };

        std::scoped_lock lock(g_lock);
        auto& state = TargetStates()[ToIndex(a_target)];
        const auto sound = ConsumePendingSound(a_target);
        const auto mode = Settings::GetSingleton()->GetExtraRingMode();

        if (!player || !ring || selection.kind == Selection::Kind::kNone) {
            const auto clearSound = sound == RingSounds::Event::kUnequip ? sound : RingSounds::Event::kNone;
            AddPendingClear(plan, BuildClearPlan(player, state, true, clearSound));
            return plan;
        }

        const auto modeChanged = state.active && state.mode != mode;
        if (modeChanged) {
            AddPendingClear(plan, BuildClearPlan(player, state, true));
        }

        const auto sourceChanged = state.sourceFormID != ring->GetFormID();
        const auto selectionChanged = state.activeSelection != selection;
        const auto changedSelection = !state.active || sourceChanged || selectionChanged || state.mode != mode;

        if (!EnsureEffectSource(a_target, *ring, selection, state, plan)) {
            logger::warn(
                "VirtualRings: effect source prepare failed | target={} | source={:08X}",
                TargetLabel(a_target),
                ring->GetFormID()
            );
            plan.clearSelection = true;
            AddPendingClear(plan, BuildClearPlan(player, state, true));
            return plan;
        }

        const auto restoredEffectSource = state.effectSource
                                          && state.effectSource->GetFormID()
                                          == selection.restoredEffectSourceFormID;
        const auto hasRestoredLoadedBinding = mode
                                              == ExtraRingMode::kFunctional
                                              && !state.active
                                              && restoredEffectSource
                                              && EventBindings::HasLoadedBinding(
                                                  ring->GetFormID(),
                                                  state.effectSource->GetFormID()
                                              );
        if (mode == ExtraRingMode::kCosmetic && !state.active && restoredEffectSource) {
            AddPendingClear(
                plan,
                PendingClear {
                    .actor = player,
                    .effectSource = state.effectSource,
                    .sourceFormID = state.sourceFormID,
                    .effectSourceFormID = state.effectSource->GetFormID(),
                    .dispatchUnequipped = true,
                    .active = true,
                }
            );
        }

        bool clearSelection = false;
        if (!ApplySelectionToEffectSource(*player, *ring, selection, mode, state, clearSelection)) {
            logger::warn(
                "VirtualRings: effect source apply failed | target={} | source={:08X}",
                TargetLabel(a_target),
                ring->GetFormID()
            );
            plan.clearSelection = clearSelection;
            auto clear = BuildClearPlan(player, state, true);
            clear.active = clear.active || hasRestoredLoadedBinding;
            AddPendingClear(plan, clear);
            return plan;
        }

        state.activeSelection = selection;
        state.mode = mode;
        state.active = true;
        state.footprint = RingFootprints::GetSourceRingFootprint(*ring);
        if (auto* playerRace = player->GetRace()) {
            state.addonVisuals = CaptureRingVisuals(*ring, a_target, *playerRace);
        } else {
            state.addonVisuals.clear();
        }

        plan.apply = PendingApply {
            .actor = player,
            .effectSource = state.effectSource,
            .extraList = state.extraList,
            .sourceFormID = ring->GetFormID(),
            .sound = sound == RingSounds::Event::kEquip ? sound : RingSounds::Event::kNone,
            .dispatchEquipped = mode == ExtraRingMode::kFunctional && changedSelection,
        };
        return plan;
    }

    void RefreshOnce() {
        std::vector<RefreshPlan> plans;
        plans.reserve(kVirtualRingTargets.size());

        for (const auto target : kVirtualRingTargets) {
            plans.push_back(BuildRefreshPlan(target));
        }

        for (auto& plan : plans) {
            for (const auto& clear : plan.clears) {
                RunPendingClear(clear);
            }
            if (plan.clearSelection) {
                Selection::Clear(plan.target);
            }
            if (plan.updateRestoredEffectSource) {
                Selection::SetRestoredEffectSourceFormID(plan.target, plan.restoredEffectSourceFormID);
            }
        }

        for (const auto& plan : plans) {
            RunPendingApply(plan.apply);
        }

        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            RingEnchantments::RefreshVanillaRingSlotEffects(*player);
            RefreshVanillaCompatibility();
        }

        RingVisuals::RequestRefresh();
    }

    void RequestRefreshImpl(const RefreshOptions& a_options) {
        bool shouldQueue = false;
        {
            std::scoped_lock lock(g_lock);
            StorePendingSound(a_options);
            if (g_refreshPending) {
                return;
            }

            g_refreshPending = true;
            shouldQueue = !g_refreshRunning;
            if (shouldQueue) {
                g_refreshRunning = true;
            }
        }

        if (!shouldQueue) {
            return;
        }

        stl::add_task([] {
            VirtualRings::Refresh();
        });
    }
}

void RequestRefresh(const RefreshOptions a_options) {
    RequestRefreshImpl(a_options);
}

void Refresh() {
    for (;;) {
        {
            std::scoped_lock lock(g_lock);
            g_refreshPending = false;
        }
        RefreshOnce();

        std::scoped_lock lock(g_lock);
        if (!g_refreshPending) {
            g_refreshRunning = false;
            return;
        }
    }
}

void Clear(const RingTarget a_target, const RingSounds::Event a_sound) {
    if (!IsVirtualRingTarget(a_target)) {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    PendingClear action;
    {
        std::scoped_lock lock(g_lock);
        g_pendingSounds[ToIndex(a_target)] = RingSounds::Event::kNone;
        action = BuildClearPlan(player, TargetStates()[ToIndex(a_target)], true, a_sound);
    }
    RunPendingClear(action);
    if (player) {
        RingEnchantments::RefreshVanillaRingSlotEffects(*player);
        RefreshVanillaCompatibility();
    }
    RequestRefreshImpl({});
    RingVisuals::RequestRefresh();
}

void Revert() {
    auto* player = RE::PlayerCharacter::GetSingleton();
    std::vector<PendingClear> actions;
    {
        std::scoped_lock lock(g_lock);
        actions.reserve(TargetStates().size());
        for (auto& state : TargetStates()) {
            actions.push_back(BuildClearPlan(player, state, false));
        }
        g_pendingSounds.fill(RingSounds::Event::kNone);
        g_refreshPending = false;
        g_refreshRunning = false;
    }
    for (const auto& action : actions) {
        RunPendingClear(action);
    }
    VanillaCompatibility::Revert();
    RingVisuals::Revert();
}

bool MatchesGetEquippedCondition([[maybe_unused]] RE::Actor& a_actor, RE::TESForm& a_getEquippedArgument) {
    std::scoped_lock lock(g_lock);
    for (const auto target : kVirtualRingTargets) {
        const auto& state = TargetStates()[ToIndex(target)];
        const auto functional = state.mode == ExtraRingMode::kFunctional;
        if (state.active && functional && SourceMatchesGetEquippedArgument(state.sourceFormID, a_getEquippedArgument)) {
            return true;
        }
    }

    return false;
}

bool IsEffectSource(const RE::TESObjectARMO* a_armor) {
    if (!a_armor) {
        return false;
    }

    std::scoped_lock lock(g_lock);
    return std::ranges::any_of(kVirtualRingTargets, [a_armor](const auto target) {
        return TargetStates()[ToIndex(target)].effectSource == a_armor;
    });
}

bool IsEnchantedVirtualRingEffectSource(const RE::TESObjectARMO* a_armor) {
    if (!a_armor) {
        return false;
    }

    std::scoped_lock lock(g_lock);
    return std::ranges::any_of(kVirtualRingTargets, [a_armor](const auto target) {
        const auto& state = TargetStates()[ToIndex(target)];
        return CountsForEnchantmentStrength(state) && state.effectSource == a_armor;
    });
}

std::uint32_t CountEnchantedVirtualRings() {
    std::scoped_lock lock(g_lock);
    auto count = std::uint32_t {0};
    for (const auto target : kVirtualRingTargets) {
        if (CountsForEnchantmentStrength(TargetStates()[ToIndex(target)])) {
            ++count;
        }
    }

    return count;
}

std::vector<VisualEntry> GetVisualEntries() {
    std::vector<VisualEntry> entries;
    std::scoped_lock lock(g_lock);
    for (const auto target : kVirtualRingTargets) {
        const auto& state = TargetStates()[ToIndex(target)];
        if (!state.active || state.sourceFormID == 0 || state.addonVisuals.empty()) {
            continue;
        }

        auto* sourceArmor = RE::TESForm::LookupByID<RE::TESObjectARMO>(state.sourceFormID);
        if (!sourceArmor) {
            continue;
        }

        entries.push_back(
            VisualEntry {
                .target = target,
                .footprint = state.footprint,
                .sourceFormID = state.sourceFormID,
                .addonVisuals = state.addonVisuals,
            }
        );
    }

    return entries;
}

std::vector<EventBindings::ScriptBindingSource> GetEventBindingSources() {
    std::vector<EventBindings::ScriptBindingSource> sources;
    std::scoped_lock lock(g_lock);
    for (const auto target : kVirtualRingTargets) {
        const auto& state = TargetStates()[ToIndex(target)];
        const auto functional = state.mode == ExtraRingMode::kFunctional;
        if (!state.active || !functional || state.sourceFormID == 0 || !state.effectSource) {
            continue;
        }

        sources.push_back(
            EventBindings::ScriptBindingSource {
                .sourceFormID = state.sourceFormID,
                .effectSourceFormID = state.effectSource->GetFormID(),
            }
        );
    }

    return sources;
}
}
