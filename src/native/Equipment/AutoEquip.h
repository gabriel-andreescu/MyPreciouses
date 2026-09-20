#pragma once

#include <RE/Skyrim.h> // IWYU pragma: keep

#include "Core/ActorKey.h"

#include <cstdint>

namespace Equipment::AutoEquip {
enum class RefreshReason : std::uint32_t {
    kContainerChanged = 1U << 0U,
    kEquipChanged = 1U << 1U,
    kLoad = 1U << 2U,
    kSettingsChanged = 1U << 3U,
    kLoad3D = 1U << 4U,
    kAutoEquipPlanRulesChanged = 1U << 5U,
};

void HandleContainerMenuOpened();
void HandleContainerMenuClosed();

[[nodiscard]] bool IsManagedActor(Core::ActorKey a_actor);
void QueueRefresh(Core::ActorKey a_actor, RefreshReason a_reason);
void QueueRefreshKnownActors(RefreshReason a_reason);
void HandleContainerChanged(const RE::TESContainerChangedEvent& a_event);
void HandleEquipEvent(RE::Actor& a_actor, RE::FormID a_sourceFormID, bool a_equipped);
void HandleActorLoad3D(RE::Actor& a_actor);
void ResumeDeferredRefresh(Core::ActorKey a_actor);
void Revert();
}
