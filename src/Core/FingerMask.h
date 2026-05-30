#pragma once

#include "Core/Target.h"

#include <bit>
#include <cstdint>
#include <utility>

namespace Core {
class FingerMask {
public:
    void Add(Finger a_finger) {
        _mask |= static_cast<std::uint8_t>(1u << std::to_underlying(a_finger));
    }

    [[nodiscard]] bool IsMultiFinger() const {
        return std::popcount(_mask) > 1;
    }

    [[nodiscard]] bool Occupies(Finger a_finger) const {
        return (_mask & static_cast<std::uint8_t>(1u << std::to_underlying(a_finger))) != 0;
    }

private:
    std::uint8_t _mask {0};
};

class TargetMask {
public:
    void Add(Target a_target) {
        _bits |= static_cast<std::uint16_t>(1u << ToIndex(a_target));
    }

    [[nodiscard]] bool Contains(Target a_target) const {
        return (_bits & static_cast<std::uint16_t>(1u << ToIndex(a_target))) != 0;
    }

    [[nodiscard]] bool Intersects(const TargetMask& a_other) const {
        return (_bits & a_other._bits) != 0;
    }

private:
    std::uint16_t _bits {0};
};
}
