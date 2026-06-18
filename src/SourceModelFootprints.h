#pragma once

#include "Core/TargetMask.h"

#include <optional>
#include <string>
#include <string_view>

namespace RE {
class NiAVObject;
class NiSkinInstance;
class TESObjectARMO;
}

namespace SourceModelFootprints {
[[nodiscard]] std::optional<std::string> RetargetSourceNodeName(
    std::string_view a_name,
    Core::Target a_target,
    Core::TargetMask a_sourceTargets
);

[[nodiscard]] bool HasOnlyMeaningfulFingerWeights(const RE::NiSkinInstance& a_skin);
[[nodiscard]] bool IsRingModel(RE::NiAVObject& a_root);
[[nodiscard]] bool IsRingModel(const RE::TESObjectARMO& a_ring);
[[nodiscard]] Core::TargetMask GetSourceTargets(const RE::TESObjectARMO& a_ring);
[[nodiscard]] Core::TargetMask GetProjectedTargets(const RE::TESObjectARMO& a_ring, Core::Target a_target);
[[nodiscard]] Core::TargetMask GetProjectedTargets(Core::TargetMask a_sourceTargets, Core::Target a_target);
}
