#include "Equipment/SpecialRingRules.h"

#include "Compatibility/Vanilla.h"
#include "Settings.h"

#include <memory>

namespace Equipment::SpecialRingRules {
namespace {
    [[nodiscard]] bool OccupiesOnlyBondLeftRingFinger(const Core::TargetMask& a_occupiedTargets) {
        return a_occupiedTargets.Count() == 1 && a_occupiedTargets.Contains(kBondOfMatrimonyLeftRingFingerTarget);
    }
}

bool ShouldUseBondOfMatrimonyLeftRingFingerAction(const Core::ActorKey a_actor, const RE::TESObjectARMO& a_ring) {
    return Core::IsPlayerActorKey(a_actor)
           && Settings::GetSingleton()->ShouldUseBondOfMatrimonyOnLeftRingFinger()
           && Compatibility::Vanilla::IsBondOfMatrimony(std::addressof(a_ring));
}

bool AreTargetsEnabledForSource(
    const Core::ActorKey a_actor,
    const RE::TESObjectARMO& a_ring,
    const Core::TargetMask& a_targets
) {
    return Settings::GetSingleton()->AreTargetsEnabled(a_targets)
           || (ShouldUseBondOfMatrimonyLeftRingFingerAction(a_actor, a_ring)
               && OccupiesOnlyBondLeftRingFinger(a_targets));
}
}
