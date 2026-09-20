#pragma once

#include <RE/Skyrim.h> // IWYU pragma: keep

#include "Core/TargetMask.h"

#include <optional>
#include <string>
#include <string_view>

namespace RE {
class NiAVObject;
class TESObjectARMO;
}

namespace SourceModelFootprints {
[[nodiscard]] std::optional<std::string> RetargetSourceNodeName(
    std::string_view a_name,
    Core::Target a_target,
    Core::TargetMask a_sourceTargets
);

[[nodiscard]] bool HasOnlyRingGeometry(RE::NiAVObject& a_root);
[[nodiscard]] bool HasRingModel(const RE::TESObjectARMO& a_armor);
[[nodiscard]] Core::TargetMask GetRingGeometrySourceTargets(const RE::TESObjectARMO& a_armor);
[[nodiscard]] Core::TargetMask GetProjectedRingGeometryTargets(const RE::TESObjectARMO& a_armor, Core::Target a_target);
[[nodiscard]] Core::TargetMask GetProjectedTargets(Core::TargetMask a_sourceTargets, Core::Target a_target);
}
