#include "Papyrus.h"

#include "Core/ActorKey.h"
#include "Settings.h"
#include "UI.h"
#include "VirtualSlots.h"

namespace Papyrus {
namespace {
    void RefreshRingsAfterSettingsChanged() {
        VirtualSlots::RequestRefresh(Core::GetPlayerActorKey());
        UI::RefreshRingItemRows();
    }

    void OnMcmConfigClose([[maybe_unused]] RE::TESQuest* a_quest) {
        const auto reload = Settings::GetSingleton()->Reload();
        if (!reload.Changed()) {
            return;
        }

        if (reload.extraRingModeChanged || reload.enchantmentStrengthChanged) {
            logger::info("Papyrus: MCM ring settings changed | action=refreshRings");
            stl::add_task(RefreshRingsAfterSettingsChanged);
            return;
        }

        if (reload.fingerSelectionChanged) {
            logger::info("Papyrus: MCM finger settings changed | action=refreshUI");
            stl::add_task(UI::RefreshRingItemRows);
        }
    }

    bool RegisterMcmNativeFunctions(RE::BSScript::IVirtualMachine* a_vm) {
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
