#include "Equipment/SpecialRingRules.h"

#include "Compatibility/Vanilla.h"
#include "Settings.h"

#include <memory>

namespace Equipment::SpecialRingRules {
namespace {
    [[nodiscard]] bool OccupiesOnlyBondLeftRingFinger(const Core::TargetMask& a_occupiedTargets) {
        return a_occupiedTargets.Count() == 1 && a_occupiedTargets.Contains(kBondOfMatrimonyLeftRingFingerTarget);
    }

    [[nodiscard]] bool ShouldAllowBondOfMatrimonyLeftRingFingerTarget(
        const Core::ActorKey a_actor,
        const RE::TESObjectARMO& a_ring
    ) {
        if (!Compatibility::Vanilla::IsBondOfMatrimony(std::addressof(a_ring))) {
            return false;
        }

        const auto* settings = Settings::GetSingleton();
        if (Core::IsPlayerActorKey(a_actor)) {
            return settings->ShouldPlayerAlwaysEquipBondOfMatrimonyOnLeftRingFinger();
        }

        return settings->IsActorVirtualRingSupportEnabled(a_actor)
               && settings->ShouldNpcAlwaysEquipBondOfMatrimonyOnLeftRingFinger();
    }
}

bool ShouldUseBondOfMatrimonyLeftRingFingerAction(const Core::ActorKey a_actor, const RE::TESObjectARMO& a_ring) {
    return Core::IsPlayerActorKey(a_actor)
           && Settings::GetSingleton()->ShouldPlayerAlwaysEquipBondOfMatrimonyOnLeftRingFinger()
           && Compatibility::Vanilla::IsBondOfMatrimony(std::addressof(a_ring));
}

bool AreTargetsEnabledForSource(
    const Core::ActorKey a_actor,
    const RE::TESObjectARMO& a_ring,
    const Core::TargetMask& a_targets
) {
    return Settings::GetSingleton()->AreTargetsEnabled(a_targets)
           || (ShouldAllowBondOfMatrimonyLeftRingFingerTarget(a_actor, a_ring)
               && OccupiesOnlyBondLeftRingFinger(a_targets));
}
}
