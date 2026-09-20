#include "Papyrus.h"

#include <RE/Skyrim.h> // IWYU pragma: keep
#include <SKSE/SKSE.h> // IWYU pragma: keep

#include "Core/ActorKey.h"
#include "Equipment/AssignmentActions.h"
#include "Equipment/AutoEquip.h"
#include "Equipment/RaceSwitchRestore.h"
#include "Equipment/SavedEquipment.h"
#include "Settings.h"
#include "UI.h"
#include "VirtualSlots.h"

#include <cstdint>
#include <utility>

namespace Papyrus {
namespace {
    void SaveExtraRings(
        [[maybe_unused]] RE::StaticFunctionTag* a_tag, // NOLINT(misc-const-correctness)
        RE::Actor* a_actor                             // NOLINT(misc-const-correctness)
    ) {
        if (a_actor) {
            Equipment::SavedEquipment::Capture(Core::MakeActorKey(*a_actor));
        }
    }

    void RestoreExtraRings(
        [[maybe_unused]] RE::StaticFunctionTag* a_tag, // NOLINT(misc-const-correctness)
        RE::Actor* a_actor                             // NOLINT(misc-const-correctness)
    ) {
        if (!a_actor) {
            return;
        }
        const auto actorKey = Core::MakeActorKey(*a_actor);
        auto snapshot = Equipment::SavedEquipment::Take(actorKey);
        if (!snapshot) {
            return;
        }
        SKSE::GetTaskInterface()->AddTask([actorKey, snapshot = std::move(*snapshot)] {
            auto* actor = Core::ResolveActor(actorKey);
            if (!actor || !Settings::GetSingleton()->IsActorVirtualRingSupportEnabled(actorKey)) {
                return;
            }
            Equipment::RestoreAvailableVirtualAssignments(*actor, snapshot);
            VirtualSlots::RequestRefresh(actorKey);
            UI::RefreshRingItemRows();
        });
    }

    bool ShouldShowVanillaControllerHintWarning(
        [[maybe_unused]] RE::TESQuest* a_quest, // NOLINT(misc-const-correctness)
        const std::int32_t a_button
    ) {
        if (a_button < 0) {
            return false;
        }

        return UI::ShouldWarnUnsupportedVanillaInventoryHint(static_cast<std::uint32_t>(a_button));
    }

    void ClearNonPlayerVirtualRingState() {
        static_cast<void>(Equipment::ClearNonPlayerVirtualAssignments());
        Equipment::RaceSwitchRestore::ClearNonPlayerState();
    }

    void RefreshRingsAfterSlotOrEffectSettingsChanged(const Settings::ReloadResult a_reload) {
        if (a_reload.virtualSlotsChanged || a_reload.playerAlwaysEquipBondOfMatrimonyLeftRingFingerChanged) {
            static_cast<void>(Equipment::ClearDisabledVirtualSlotAssignments());
        }

        if (a_reload.npcSupportChanged && !a_reload.npcSupportEnabled) {
            ClearNonPlayerVirtualRingState();
        }

        VirtualSlots::RequestRefresh(
            Core::GetPlayerActorKey(),
            VirtualSlots::RefreshOptions {
                .soundTarget = std::nullopt,
                .sound = Audio::EquipSounds::Cue::kNone,
                .preserveLoadedEffects = false,
                .reapplyEffects = true,
                .restoreMissingEffects = false,
            }
        );
        Equipment::AutoEquip::QueueRefreshKnownActors(Equipment::AutoEquip::RefreshReason::kSettingsChanged);
        UI::RefreshRingItemRows();
    }

    void OnMcmConfigClose(
        [[maybe_unused]] RE::TESQuest* a_quest // NOLINT(misc-const-correctness)
    ) {
        const auto reload = Settings::GetSingleton()->Reload();
        if (reload.unequipAllClearsExtraRingsChanged && !reload.unequipAllClearsExtraRingsEnabled) {
            Equipment::RaceSwitchRestore::ClearActiveSwitches();
        }

        if (!reload.Changed()) {
            return;
        }

        const auto npcSupportDisabled = reload.npcSupportChanged && !reload.npcSupportEnabled;
        const auto slotOrEffectSettingsChanged = reload.extraRingModeChanged
                                                 || reload.enchantmentStrengthChanged
                                                 || reload.playerAlwaysEquipBondOfMatrimonyLeftRingFingerChanged
                                                 || reload.virtualSlotsChanged;
        if (slotOrEffectSettingsChanged) {
            SKSE::log::info(
                "Papyrus: MCM ring settings changed | action=refreshRings | clearNonPlayer={}",
                npcSupportDisabled
            );
            SKSE::GetTaskInterface()->AddTask([reload] { RefreshRingsAfterSlotOrEffectSettingsChanged(reload); });
            return;
        }

        if (npcSupportDisabled) {
            SKSE::log::info("Papyrus: MCM NPC support disabled | action=clearNonPlayerVirtualRings");
            SKSE::GetTaskInterface()->AddTask([] {
                ClearNonPlayerVirtualRingState();
                UI::RefreshRingItemRows();
            });
            return;
        }

        const auto npcSupportEnabled = reload.npcSupportChanged && reload.npcSupportEnabled;
        const auto npcAutoEquipRulesChanged = reload.npcAlwaysEquipBondOfMatrimonyLeftRingFingerChanged
                                              || npcSupportEnabled;
        if (npcAutoEquipRulesChanged) {
            SKSE::log::info("Papyrus: MCM NPC auto-equip rules changed | action=refreshActors");
            SKSE::GetTaskInterface()->AddTask([refreshFingerSelectionUi = reload.fingerSelectionChanged] {
                Equipment::AutoEquip::QueueRefreshKnownActors(
                    Equipment::AutoEquip::RefreshReason::kAutoEquipPlanRulesChanged
                );
                if (refreshFingerSelectionUi) {
                    UI::RefreshRingItemRows();
                }
            });
            return;
        }

        if (reload.fingerSelectionChanged) {
            SKSE::log::info("Papyrus: MCM finger settings changed | action=refreshUI");
            SKSE::GetTaskInterface()->AddTask(UI::RefreshRingItemRows);
        }
    }

    bool RegisterNativeFunctions(RE::BSScript::IVirtualMachine* a_vm) {
        a_vm->RegisterFunction("SaveExtraRings", "MyPreciouses", SaveExtraRings);
        a_vm->RegisterFunction("RestoreExtraRings", "MyPreciouses", RestoreExtraRings);
        a_vm->RegisterFunction(
            "ShouldShowVanillaControllerHintWarning",
            "MyPreciouses_MCM",
            ShouldShowVanillaControllerHintWarning
        );
        a_vm->RegisterFunction("OnConfigCloseNative", "MyPreciouses_MCM", OnMcmConfigClose);
        SKSE::log::info("Papyrus: equipment API and MCM callbacks registered");
        return true;
    }
}

void Register() {
    const auto* papyrus = SKSE::GetPapyrusInterface();
    if (!papyrus) {
        SKSE::log::critical("Papyrus: register skipped | reason=noInterface");
        return;
    }

    papyrus->Register(RegisterNativeFunctions);
}
}
