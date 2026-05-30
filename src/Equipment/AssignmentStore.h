#pragma once

#include "Core/Assignment.h"
#include "Core/Target.h"

#include <optional>
#include <vector>

namespace Equipment::AssignmentStore {
[[nodiscard]] bool AssignForm(Core::ActorKey a_actor, RE::TESObjectARMO& a_ring, Core::Target a_target);
[[nodiscard]] bool AssignCustom(
    Core::ActorKey a_actor,
    RE::TESObjectARMO& a_ring,
    Core::CustomEnchantmentSignature a_signature,
    std::optional<Core::ExtraUniqueIDKey> a_uniqueID,
    Core::Target a_target
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
[[nodiscard]] std::uint32_t CountMatching(
    Core::ActorKey a_actor,
    const Core::ItemSource& a_source,
    std::optional<Core::Target> a_excludedTarget = std::nullopt
);
[[nodiscard]] bool ContainsSource(Core::ActorKey a_actor, RE::FormID a_sourceFormID);

void ReplaceAll(std::vector<Core::ActorAssignments> a_snapshots);
void Revert();
}
