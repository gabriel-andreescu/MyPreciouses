#include "FirstPerson.h"

#include "Settings.h"
#include "Slots.h"

namespace FirstPerson {
void ApplyRaceFlags() {
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) {
        logger::warn("FirstPerson: race flag patch skipped | reason=noDataHandler");
        return;
    }

    std::uint32_t patched = 0;
    std::uint32_t changedFlags = 0;
    for (auto* race : dataHandler->GetFormArray<RE::TESRace>()) {
        if (!race) {
            continue;
        }

        auto& firstPersonFlags = race->bipedModelData.bipedObjectSlots;
        bool changedRace = false;
        if (!firstPersonFlags.all(RE::BGSBipedObjectForm::FirstPersonFlag::kRing)) {
            firstPersonFlags.set(RE::BGSBipedObjectForm::FirstPersonFlag::kRing);
            changedRace = true;
            ++changedFlags;
        }
        const auto leftRingFlag = Slots::GetFirstPersonFlag();
        if (!firstPersonFlags.all(leftRingFlag)) {
            firstPersonFlags.set(leftRingFlag);
            changedRace = true;
            ++changedFlags;
        }

        if (Settings::GetSingleton()->IsBondOfMatrimonyEnabled()) {
            const auto bondRingFlag = Slots::GetFirstPersonFlag(DisplaySlot::kBond);
            if (!firstPersonFlags.all(bondRingFlag)) {
                firstPersonFlags.set(bondRingFlag);
                changedRace = true;
                ++changedFlags;
            }
        }
        if (changedRace) {
            ++patched;
        }
    }

    logger::info("FirstPerson: race flags patched | races={} | flags={}", patched, changedFlags);
}
}
