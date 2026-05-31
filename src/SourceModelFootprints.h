#pragma once

#include "Core/FingerMask.h"

#include <optional>
#include <string>
#include <string_view>

namespace RE {
class NiAVObject;
class NiSkinInstance;
class TESObjectARMO;
}

namespace SourceModelFootprints {
[[nodiscard]] std::optional<std::string> RetargetFingerBoneName(
    std::string_view a_name,
    Core::Target a_target,
    const Core::FingerMask& a_sourceFingerMask
);

[[nodiscard]] bool HasOnlyMeaningfulFingerWeights(const RE::NiSkinInstance& a_skin);
[[nodiscard]] bool IsRingModel(RE::NiAVObject& a_root);
[[nodiscard]] Core::FingerMask GetSourceFingerMask(const RE::TESObjectARMO& a_ring);
[[nodiscard]] Core::TargetMask GetProjectedTargets(const RE::TESObjectARMO& a_ring, Core::Target a_target);
[[nodiscard]] Core::TargetMask GetProjectedTargets(const Core::FingerMask& a_sourceFingerMask, Core::Target a_target);
}
