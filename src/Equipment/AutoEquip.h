#pragma once

#include "Core/ActorKey.h"

#include <cstdint>
#include <optional>

namespace Equipment::AutoEquip {
enum class RefreshReason : std::uint32_t {
    kContainerChanged = 1U << 0,
    kEquipChanged = 1U << 1,
    kLoad = 1U << 2,
    kSettingsChanged = 1U << 3,
    kLoad3D = 1U << 4,
};

void HandleContainerMenuOpened();
void HandleContainerMenuClosed();
[[nodiscard]] std::optional<Core::ActorKey> GetFollowerTradeTarget();

[[nodiscard]] bool IsManagedActor(Core::ActorKey a_actor);
void QueueRefresh(Core::ActorKey a_actor, RefreshReason a_reason);
void QueueRefreshStoredActors(RefreshReason a_reason);
void HandleContainerChanged(const RE::TESContainerChangedEvent& a_event);
void HandleEquipEvent(RE::Actor& a_actor, RE::FormID a_sourceFormID);
void HandleActorLoad3D(RE::Actor& a_actor);
void Revert();
}
