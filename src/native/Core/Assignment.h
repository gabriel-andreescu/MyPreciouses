#pragma once

#include <RE/Skyrim.h> // IWYU pragma: keep

#include "Core/ActorKey.h"
#include "Core/ItemSource.h"
#include "Core/Target.h"

#include <array>

namespace Core {
struct Assignment {
    ItemSource source;
    RE::FormID retainedEffectSourceFormID {0};
    bool needsCopyBinding {false};

    [[nodiscard]] bool IsAssigned() const {
        return source.IsAssigned();
    }

    [[nodiscard]] bool operator==(const Assignment&) const = default;
};

struct TargetAssignments {
    std::array<Assignment, kAllTargets.size()> byTarget;

    bool RemapUniqueID(const ExtraUniqueIDKey& a_previous, const ExtraUniqueIDKey& a_next) {
        auto changed = false;
        for (auto& assignment : byTarget) {
            if (assignment.source.extraUniqueID == a_previous) {
                assignment.source.extraUniqueID = a_next;
                changed = true;
            }
        }
        return changed;
    }
};

struct ActorAssignments {
    ActorKey actor;
    TargetAssignments assignments;
};
}
