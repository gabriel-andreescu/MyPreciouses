#include "Papyrus.h"

#include "Settings.h"
#include "UI.h"
#include "VirtualRings.h"

namespace Papyrus {
namespace {
    void RefreshAfterRingSettingsReload() {
        VirtualRings::RequestRefresh();
        UI::RefreshRingRows();
    }

    void OnConfigCloseNative([[maybe_unused]] RE::TESQuest* a_quest) {
        const auto reload = Settings::GetSingleton()->Reload();
        if (!reload.Changed()) {
            return;
        }

        if (reload.extraRingModeChanged || reload.enchantmentStrengthChanged) {
            logger::info("Settings: ring behavior changed | action=refreshVirtualRings");
            stl::add_task(RefreshAfterRingSettingsReload);
        }
    }

    bool RegisterMCM(RE::BSScript::IVirtualMachine* a_vm) {
        a_vm->RegisterFunction("OnConfigCloseNative", "LeftHandRingsSKSE_MCM", OnConfigCloseNative);
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

    papyrus->Register(RegisterMCM);
}
}
