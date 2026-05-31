#pragma once

#include "Core/Assignment.h"
#include "Core/Target.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace Equipment {
enum class ActionBlockReason : std::uint8_t {
    kNone = 0,
    kRightHandRingCannotBeUnequipped,
};

struct SourceSelection {
    Core::ActorKey actor;
    Core::ItemSource itemSource;
    RE::ExtraDataList* preferredExtraList {nullptr};
};

struct ActionResult {
    bool selectionChanged {false};
    bool inventoryChanged {false};
    bool sourceUnavailable {false};
    bool handled {false};
    ActionBlockReason blockReason {ActionBlockReason::kNone};

    [[nodiscard]] bool ChangedState() const {
        return selectionChanged || inventoryChanged;
    }

    [[nodiscard]] bool WasHandled() const {
        return handled || ChangedState() || sourceUnavailable;
    }
};

using CompletionCallback = std::function<void(ActionResult)>;

enum class QueueMode : std::uint8_t {
    kImmediate = 0,
    kQueued,
};

[[nodiscard]] bool IsSelected(const SourceSelection& a_selection, Core::Target a_target);
[[nodiscard]] bool IsInVanillaRingSlot(const SourceSelection& a_selection);
[[nodiscard]] bool IsProtectedInVanillaRingSlot(const SourceSelection& a_selection);
[[nodiscard]] std::vector<Core::Target> CollectSelectedTargetsOnHand(
    const SourceSelection& a_selection,
    Core::Hand a_hand
);
[[nodiscard]] ActionResult ToggleTarget(
    const SourceSelection& a_selection,
    Core::Target a_target,
    std::optional<Core::Target> a_moveSourceTarget = std::nullopt,
    QueueMode a_queueMode = QueueMode::kImmediate,
    CompletionCallback a_onQueuedComplete = {}
);

[[nodiscard]] bool InterceptRightEquip(
    RE::Actor& a_actor,
    const RE::TESObjectARMO& a_ring,
    const RE::ObjectEquipParams& a_params,
    CompletionCallback a_onQueuedComplete = {}
);
void QueueAssignmentReconciliation(Core::ActorKey a_actor, CompletionCallback a_onComplete = {});
void HandleContainerChangedForAssignments(
    Core::ActorKey a_actor,
    const RE::TESContainerChangedEvent& a_event,
    CompletionCallback a_onComplete = {}
);
}
