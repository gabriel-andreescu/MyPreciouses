#pragma once

#include <RE/Skyrim.h> // IWYU pragma: keep

#include "Core/Assignment.h"
#include "Core/Target.h"
#include "Core/TargetMask.h"

#include <optional>
#include <vector>

namespace Equipment::AssignmentStore {
void RemapUniqueID(const Core::ExtraUniqueIDKey& a_previous, const Core::ExtraUniqueIDKey& a_next);
[[nodiscard]] bool Assign(
    Core::ActorKey a_actor,
    RE::TESObjectARMO const& a_ring,
    const Core::ItemSource& a_source,
    Core::Target a_target,
    std::optional<Core::Target> a_moveSourceTarget = std::nullopt
);
void Clear(Core::ActorKey a_actor, Core::Target a_target);
[[nodiscard]] bool TrySetRetainedEffectSourceFormID(
    Core::ActorKey a_actor,
    Core::Target a_target,
    const Core::Assignment& a_expectedAssignment,
    RE::FormID a_effectSourceFormID
);

[[nodiscard]] Core::Assignment Get(Core::ActorKey a_actor, Core::Target a_target);
[[nodiscard]] Core::TargetAssignments GetSnapshot(Core::ActorKey a_actor);
[[nodiscard]] std::vector<Core::ActorAssignments> GetAllSnapshots();
[[nodiscard]] Core::TargetMask GetMatchingTargets(Core::ActorKey a_actor, const Core::ItemSource& a_source);
[[nodiscard]] std::uint32_t CountMatching(
    Core::ActorKey a_actor,
    const Core::ItemSource& a_source,
    std::optional<Core::Target> a_excludedTarget = std::nullopt
);
[[nodiscard]] bool ContainsSource(Core::ActorKey a_actor, RE::FormID a_sourceFormID);

void ReplaceAll(std::vector<Core::ActorAssignments> a_snapshots);
void Revert();
}
