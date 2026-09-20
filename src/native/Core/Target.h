#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Core {
enum class Hand : std::uint8_t {
    kLeft = 0,
    kRight = 1,
};

enum class Finger : std::uint8_t {
    kThumb = 0,
    kIndex = 1,
    kMiddle = 2,
    kRing = 3,
    kPinky = 4,
};

inline constexpr std::array kFingers {
    Finger::kThumb,
    Finger::kIndex,
    Finger::kMiddle,
    Finger::kRing,
    Finger::kPinky,
};

struct Target {
    Hand hand {Hand::kLeft};
    Finger finger {Finger::kIndex};

    [[nodiscard]] bool operator==(const Target&) const = default;
};

inline constexpr std::array kAllTargets {
    Target {.hand = Hand::kLeft, .finger = Finger::kThumb},
    Target {.hand = Hand::kLeft, .finger = Finger::kIndex},
    Target {.hand = Hand::kLeft, .finger = Finger::kMiddle},
    Target {.hand = Hand::kLeft, .finger = Finger::kRing},
    Target {.hand = Hand::kLeft, .finger = Finger::kPinky},
    Target {.hand = Hand::kRight, .finger = Finger::kThumb},
    Target {.hand = Hand::kRight, .finger = Finger::kIndex},
    Target {.hand = Hand::kRight, .finger = Finger::kMiddle},
    Target {.hand = Hand::kRight, .finger = Finger::kRing},
    Target {.hand = Hand::kRight, .finger = Finger::kPinky},
};

inline constexpr auto kDefaultLeftTarget = Target {.hand = Hand::kLeft, .finger = Finger::kIndex};
inline constexpr auto kVanillaRingSlotTarget = Target {.hand = Hand::kRight, .finger = Finger::kIndex};

inline constexpr std::array kVirtualTargets {
    Target {.hand = Hand::kLeft, .finger = Finger::kThumb},
    Target {.hand = Hand::kLeft, .finger = Finger::kIndex},
    Target {.hand = Hand::kLeft, .finger = Finger::kMiddle},
    Target {.hand = Hand::kLeft, .finger = Finger::kRing},
    Target {.hand = Hand::kLeft, .finger = Finger::kPinky},
    Target {.hand = Hand::kRight, .finger = Finger::kThumb},
    Target {.hand = Hand::kRight, .finger = Finger::kMiddle},
    Target {.hand = Hand::kRight, .finger = Finger::kRing},
    Target {.hand = Hand::kRight, .finger = Finger::kPinky},
};

[[nodiscard]] constexpr std::uint32_t ToIndex(const Target a_target) {
    const auto handOffset = a_target.hand == Hand::kLeft ? 0U : 5U;
    return handOffset + static_cast<std::uint32_t>(a_target.finger);
}

[[nodiscard]] constexpr std::optional<Target> FromIndex(const std::uint32_t a_index) {
    if (a_index >= kAllTargets.size()) {
        return std::nullopt;
    }

    return kAllTargets[a_index];
}

[[nodiscard]] constexpr bool IsVirtualTarget(const Target a_target) {
    return ToIndex(a_target) != ToIndex(kVanillaRingSlotTarget);
}

[[nodiscard]] constexpr std::string_view TargetName(const Target a_target) {
    switch (ToIndex(a_target)) {
        case 0:  return "leftThumb";
        case 1:  return "leftIndex";
        case 2:  return "leftMiddle";
        case 3:  return "leftRing";
        case 4:  return "leftPinky";
        case 5:  return "rightThumb";
        case 6:  return "rightIndex";
        case 7:  return "rightMiddle";
        case 8:  return "rightRing";
        case 9:  return "rightPinky";
        default: return "unknown";
    }
}
}
