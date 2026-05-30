#pragma once

#include "RingTargets.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace RE {
class NiSkinInstance;
class TESObjectARMO;
}

namespace RingFootprints {
struct RingBone {
    RingHand hand {RingHand::kRight};
    RingFinger finger {RingFinger::kIndex};
    std::uint8_t segment {0};

    [[nodiscard]] bool operator==(const RingBone&) const = default;
};

class RingFootprint {
public:
    void Add(RingFinger a_finger);
    void Merge(const RingFootprint& a_other);

    [[nodiscard]] bool IsKnown() const;
    [[nodiscard]] bool IsMultiFinger() const;
    [[nodiscard]] bool Occupies(RingFinger a_finger) const;
    [[nodiscard]] std::uint8_t Mask() const;

private:
    std::uint8_t _mask {0};
    bool _known {false};
};

class RingTargetMask {
public:
    void Add(RingTarget a_target);

    [[nodiscard]] bool Empty() const;
    [[nodiscard]] bool Contains(RingTarget a_target) const;
    [[nodiscard]] bool Intersects(const RingTargetMask& a_other) const;
    [[nodiscard]] std::uint16_t Bits() const;

private:
    std::uint16_t _bits {0};
};

[[nodiscard]] std::optional<RingBone> ParseFingerBoneName(std::string_view a_name);
[[nodiscard]] std::string MakeFingerBoneName(const RingBone& a_bone);
[[nodiscard]] std::optional<RingBone> RetargetBone(
    const RingBone& a_source,
    RingTarget a_target,
    const RingFootprint& a_footprint
);
[[nodiscard]] bool HasOnlyMeaningfulFingerWeights(const RE::NiSkinInstance& a_skin);

[[nodiscard]] RingFootprint GetSourceRingFootprint(const RE::TESObjectARMO& a_ring);
[[nodiscard]] RingTargetMask GetOccupiedTargets(const RE::TESObjectARMO& a_ring, RingTarget a_target);
[[nodiscard]] RingTargetMask GetOccupiedTargets(const RingFootprint& a_footprint, RingTarget a_target);
}
