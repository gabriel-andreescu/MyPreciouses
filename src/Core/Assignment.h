#pragma once

#include "Core/ActorKey.h"
#include "Core/ItemSource.h"
#include "Core/Target.h"

#include <array>

namespace Core {
struct Assignment {
    ItemSource source;
    RE::FormID retainedEffectSourceFormID {0};

    [[nodiscard]] bool IsAssigned() const {
        return source.IsAssigned();
    }

    [[nodiscard]] bool operator==(const Assignment&) const = default;
};

struct TargetAssignments {
    std::array<Assignment, kAllTargets.size()> byTarget;
};

struct ActorAssignments {
    ActorKey actor;
    TargetAssignments assignments;
};
}
