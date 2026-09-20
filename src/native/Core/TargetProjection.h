#pragma once

#include "Core/TargetMask.h"

#include <cstddef>
#include <optional>
#include <utility>

namespace Core {
[[nodiscard]] constexpr Hand OppositeHand(const Hand a_hand) {
    return a_hand == Hand::kLeft ? Hand::kRight : Hand::kLeft;
}

[[nodiscard]] constexpr std::optional<Target> FindSourceAnchor(const TargetMask a_sourceTargets) {
    for (const auto finger : kFingers) {
        const auto rightTarget = Target {.hand = Hand::kRight, .finger = finger};
        if (a_sourceTargets.Contains(rightTarget)) {
            return rightTarget;
        }

        const auto leftTarget = Target {.hand = Hand::kLeft, .finger = finger};
        if (a_sourceTargets.Contains(leftTarget)) {
            return leftTarget;
        }
    }

    return std::nullopt;
}

[[nodiscard]] constexpr std::optional<Target> ProjectSourceTarget(
    const Target a_sourceTarget,
    const Target a_sourceAnchor,
    const Target a_target
) {
    const auto sourceFinger = static_cast<int>(std::to_underlying(a_sourceTarget.finger));
    const auto anchorFinger = static_cast<int>(std::to_underlying(a_sourceAnchor.finger));
    const auto targetFinger = static_cast<int>(std::to_underlying(a_target.finger));
    const auto projectedFinger = targetFinger + sourceFinger - anchorFinger;
    if (projectedFinger < 0 || std::cmp_greater_equal(projectedFinger, kFingers.size())) {
        return std::nullopt;
    }

    return Target {
        .hand = a_sourceTarget.hand == a_sourceAnchor.hand ? a_target.hand : OppositeHand(a_target.hand),
        .finger = kFingers[static_cast<std::size_t>(projectedFinger)],
    };
}

[[nodiscard]] constexpr TargetMask ProjectSourceTargets(const TargetMask a_sourceTargets, const Target a_target) {
    TargetMask mask;
    if (a_sourceTargets.Empty()) {
        mask.Add(a_target);
        return mask;
    }

    const auto sourceAnchor = FindSourceAnchor(a_sourceTargets);
    if (!sourceAnchor) {
        return {};
    }

    for (const auto sourceTarget : kAllTargets) {
        if (!a_sourceTargets.Contains(sourceTarget)) {
            continue;
        }

        const auto target = ProjectSourceTarget(sourceTarget, *sourceAnchor, a_target);
        if (!target) {
            return {};
        }

        mask.Add(*target);
    }

    return mask;
}

static_assert([] {
    TargetMask expected;
    expected.Add(Target {.hand = Hand::kLeft, .finger = Finger::kMiddle});
    return ProjectSourceTargets({}, Target {.hand = Hand::kLeft, .finger = Finger::kMiddle}) == expected;
}());

static_assert([] {
    TargetMask source;
    source.Add(Target {.hand = Hand::kRight, .finger = Finger::kRing});

    TargetMask expected;
    expected.Add(kVanillaRingSlotTarget);
    return ProjectSourceTargets(source, kVanillaRingSlotTarget) == expected;
}());

static_assert([] {
    TargetMask source;
    source.Add(Target {.hand = Hand::kLeft, .finger = Finger::kIndex});
    source.Add(Target {.hand = Hand::kLeft, .finger = Finger::kMiddle});
    source.Add(Target {.hand = Hand::kRight, .finger = Finger::kIndex});
    source.Add(Target {.hand = Hand::kRight, .finger = Finger::kMiddle});
    source.Add(Target {.hand = Hand::kRight, .finger = Finger::kRing});
    return ProjectSourceTargets(source, kVanillaRingSlotTarget) == source;
}());

static_assert([] {
    TargetMask source;
    source.Add(Target {.hand = Hand::kRight, .finger = Finger::kIndex});
    source.Add(Target {.hand = Hand::kRight, .finger = Finger::kMiddle});

    TargetMask expected;
    expected.Add(Target {.hand = Hand::kLeft, .finger = Finger::kRing});
    expected.Add(Target {.hand = Hand::kLeft, .finger = Finger::kPinky});
    return ProjectSourceTargets(source, Target {.hand = Hand::kLeft, .finger = Finger::kRing}) == expected;
}());

static_assert([] {
    TargetMask source;
    source.Add(Target {.hand = Hand::kRight, .finger = Finger::kIndex});
    source.Add(Target {.hand = Hand::kRight, .finger = Finger::kMiddle});
    return ProjectSourceTargets(source, Target {.hand = Hand::kLeft, .finger = Finger::kPinky}).Empty();
}());
}
