#pragma once

#include "Core/ActorKey.h"
#include "Core/TargetMask.h"

namespace Equipment::SpecialRingRules {
inline constexpr Core::Target kBondOfMatrimonyLeftRingFingerTarget {
    .hand = Core::Hand::kLeft,
    .finger = Core::Finger::kRing,
};

[[nodiscard]] bool ShouldUseBondOfMatrimonyLeftRingFingerAction(
    Core::ActorKey a_actor,
    const RE::TESObjectARMO& a_ring
);
[[nodiscard]] bool AreTargetsEnabledForSource(
    Core::ActorKey a_actor,
    const RE::TESObjectARMO& a_ring,
    const Core::TargetMask& a_targets
);
}
