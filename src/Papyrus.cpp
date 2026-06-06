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

    void RefreshRingsAfterSettingsChanged() {
        VirtualSlots::RequestRefresh(
            Core::GetPlayerActorKey(),
            VirtualSlots::RefreshOptions {
                .reapplyEffects = true,
            }
        );
        Equipment::AutoEquip::QueueRefreshStoredActors(Equipment::AutoEquip::RefreshReason::kSettingsChanged);
        UI::RefreshRingItemRows();
    }

    void RefreshRingsAfterVirtualSlotsChanged() {
        static_cast<void>(Equipment::ClearDisabledVirtualSlotAssignments());
        RefreshRingsAfterSettingsChanged();
    }

    void OnMcmConfigClose([[maybe_unused]] RE::TESQuest* a_quest) {
        const auto reload = Settings::GetSingleton()->Reload();
        if (reload.unequipAllClearsExtraRingsChanged && !reload.unequipAllClearsExtraRingsEnabled) {
            Equipment::RaceSwitchRestore::ClearActiveSwitches();
        }

        if (!reload.Changed()) {
            return;
        }

        if (reload.extraRingModeChanged || reload.enchantmentStrengthChanged || reload.virtualSlotsChanged) {
            logger::info("Papyrus: MCM ring settings changed | action=refreshRings");
            stl::add_task(
                reload.virtualSlotsChanged ? RefreshRingsAfterVirtualSlotsChanged : RefreshRingsAfterSettingsChanged
            );
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
