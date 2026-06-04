#pragma once

#include "Core/Target.h"

#include <bit>
#include <cstdint>

namespace Core {
class TargetMask {
public:
    constexpr void Add(const Target a_target) {
        _bits |= static_cast<std::uint16_t>(1u << ToIndex(a_target));
    }

    [[nodiscard]] constexpr bool Contains(const Target a_target) const {
        return (_bits & static_cast<std::uint16_t>(1u << ToIndex(a_target))) != 0;
    }

    [[nodiscard]] constexpr bool Intersects(const TargetMask& a_other) const {
        return (_bits & a_other._bits) != 0;
    }

    [[nodiscard]] constexpr bool Empty() const {
        return _bits == 0;
    }

    [[nodiscard]] constexpr int Count() const {
        return std::popcount(_bits);
    }

    [[nodiscard]] constexpr std::uint16_t Bits() const {
        return _bits;
    }

    [[nodiscard]] constexpr bool operator==(const TargetMask&) const = default;

private:
    std::uint16_t _bits {0};
};
}
