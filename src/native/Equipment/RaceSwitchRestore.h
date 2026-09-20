#pragma once

#include <RE/Skyrim.h> // IWYU pragma: keep

#include "Core/Assignment.h"
#include "Core/TargetMask.h"

#include <optional>
#include <vector>

namespace Equipment::RaceSwitchRestore {
struct PendingRestore {
    Core::ActorKey actor;
    RE::FormID raceFormID {0};
    Core::TargetAssignments assignments;
};

void BeginRaceSwitch(RE::Actor const& a_actor, RE::TESRace const& a_targetRace);
bool MarkClearedDuringRaceSwitch(RE::Actor const& a_actor);
void DiscardReplacedTargets(Core::ActorKey a_actor, const Core::TargetMask& a_targets);
[[nodiscard]] bool HandleRaceSwitchComplete(RE::Actor& a_actor);
[[nodiscard]] std::optional<PendingRestore> GetPendingRestore(Core::ActorKey a_actor);
[[nodiscard]] std::vector<PendingRestore> GetPendingRestores();
void ReplacePendingRestores(std::vector<PendingRestore> a_restores);
void ClearActiveSwitches();
void ClearNonPlayerState();
void Revert();
}
