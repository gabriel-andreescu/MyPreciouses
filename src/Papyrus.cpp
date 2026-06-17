#include "Papyrus.h"

#include "Core/ActorKey.h"
#include "Equipment/AssignmentActions.h"
#include "Equipment/AutoEquip.h"
#include "Equipment/RaceSwitchRestore.h"
#include "Settings.h"
#include "UI.h"
#include "VirtualSlots.h"

#include <cstdint>

namespace Papyrus {
namespace {
    bool ShouldShowVanillaControllerHintWarning([[maybe_unused]] RE::TESQuest* a_quest, const std::int32_t a_button) {
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
        if (a_reload.virtualSlotsChanged || a_reload.bondOfMatrimonyLeftRingFingerChanged) {
            static_cast<void>(Equipment::ClearDisabledVirtualSlotAssignments());
        }

        if (a_reload.npcSupportChanged && !a_reload.npcSupportEnabled) {
            ClearNonPlayerVirtualRingState();
        }

        VirtualSlots::RequestRefresh(
            Core::GetPlayerActorKey(),
            VirtualSlots::RefreshOptions {
                .reapplyEffects = true,
            }
        );
        Equipment::AutoEquip::QueueRefreshKnownActors(Equipment::AutoEquip::RefreshReason::kSettingsChanged);
        UI::RefreshRingItemRows();
    }

    void OnMcmConfigClose([[maybe_unused]] RE::TESQuest* a_quest) {
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
                                                 || reload.bondOfMatrimonyLeftRingFingerChanged
                                                 || reload.virtualSlotsChanged;
        if (slotOrEffectSettingsChanged) {
            logger::info(
                "Papyrus: MCM ring settings changed | action=refreshRings | clearNonPlayer={}",
                npcSupportDisabled
            );
            stl::add_task([reload] {
                RefreshRingsAfterSlotOrEffectSettingsChanged(reload);
            });
            return;
        }

        if (npcSupportDisabled) {
            logger::info("Papyrus: MCM NPC support disabled | action=clearNonPlayerVirtualRings");
            stl::add_task([] {
                ClearNonPlayerVirtualRingState();
                UI::RefreshRingItemRows();
            });
            return;
        }

        const auto npcSupportEnabled = reload.npcSupportChanged && reload.npcSupportEnabled;
        const auto npcAutoEquipRulesChanged = reload.npcBondOfMatrimonyLeftRingFingerPreferenceChanged
                                              || npcSupportEnabled;
        if (npcAutoEquipRulesChanged) {
            logger::info("Papyrus: MCM NPC auto-equip rules changed | action=refreshActors");
            stl::add_task([refreshFingerSelectionUi = reload.fingerSelectionChanged] {
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
            logger::info("Papyrus: MCM finger settings changed | action=refreshUI");
            stl::add_task(UI::RefreshRingItemRows);
        }
    }

    bool RegisterMcmNativeFunctions(RE::BSScript::IVirtualMachine* a_vm) {
        a_vm->RegisterFunction(
            "ShouldShowVanillaControllerHintWarning",
            "LeftHandRingsSKSE_MCM",
            ShouldShowVanillaControllerHintWarning
        );
        a_vm->RegisterFunction("OnConfigCloseNative", "LeftHandRingsSKSE_MCM", OnMcmConfigClose);
        logger::info("Papyrus: MCM reload callback registered");
        return true;
    }
}

void Register() {
    const auto* papyrus = SKSE::GetPapyrusInterface();
    if (!papyrus) {
        logger::critical("Papyrus: register skipped | reason=noInterface");
        return;
    }

    papyrus->Register(RegisterMcmNativeFunctions);
}
}
