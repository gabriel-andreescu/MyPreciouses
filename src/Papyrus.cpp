#include "Papyrus.h"

#include "ArmorClones.h"
#include "BondOfMatrimony.h"
#include "ClonedEquipment.h"
#include "FirstPerson.h"
#include "Selection.h"
#include "Settings.h"
#include "UI.h"

namespace Papyrus {
namespace {
    void RefreshDisplaySlotAfterSettingsReload(const DisplaySlot a_channel) {
        ClonedEquipment::Clear(a_channel);
        ArmorClones::Revert(a_channel);
        ClonedEquipment::DiscardState(a_channel);
    }

    void RefreshAfterDisplaySlotSettingsReload(Settings::ReloadResult a_reload) {
        const auto normalHadBond = BondOfMatrimony::IsBond(Selection::Get(DisplaySlot::kRegular).sourceFormID);
        Selection::NormalizeAfterSettingsReload();

        if (a_reload.bipedSlotChanged || normalHadBond) {
            RefreshDisplaySlotAfterSettingsReload(DisplaySlot::kRegular);
        }

        if (a_reload.bondEnabledChanged || a_reload.bondBipedSlotChanged) {
            RefreshDisplaySlotAfterSettingsReload(DisplaySlot::kBond);
        }

        FirstPerson::ApplyRaceFlags();
        ClonedEquipment::RequestRefresh();
        UI::RefreshRows();
    }

    void RefreshAfterEnchantmentPowerSettingsReload() {
        ClonedEquipment::RequestRefresh();
        UI::RefreshRows();
    }

    void OnConfigClose([[maybe_unused]] RE::TESQuest* a_quest) {
        const auto reload = Settings::GetSingleton()->Reload();
        if (!reload.Changed()) {
            return;
        }

        if (reload.DisplaySlotsChanged()) {
            logger::info("Settings: display slots changed | action=refreshClonedEquipment");
            stl::add_task([reload] {
                RefreshAfterDisplaySlotSettingsReload(reload);
            });
            return;
        }

        if (reload.enchantmentPowerChanged) {
            logger::info("Settings: enchantment power changed | action=refreshClonedEquipment");
            stl::add_task(RefreshAfterEnchantmentPowerSettingsReload);
        }
    }

    bool RegisterMCM(RE::BSScript::IVirtualMachine* a_vm) {
        a_vm->RegisterFunction("OnConfigClose", "LeftHandRingsSKSE_MCM", OnConfigClose);
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
