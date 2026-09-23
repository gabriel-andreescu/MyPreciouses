#pragma once

#include "Core/ActorKey.h"
#include "Core/Assignment.h"

#include <optional>
#include <vector>

namespace Equipment::SavedEquipment {
void RemapUniqueID(const Core::ExtraUniqueIDKey& a_previous, const Core::ExtraUniqueIDKey& a_next);
void Capture(Core::ActorKey a_actor);
[[nodiscard]] std::optional<Core::TargetAssignments> Take(Core::ActorKey a_actor);
[[nodiscard]] std::vector<Core::ActorAssignments> GetAll();
void ReplaceAll(std::vector<Core::ActorAssignments> a_snapshots);
void Revert();
}
