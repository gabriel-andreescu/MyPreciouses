#pragma once

#include "Core/Assignment.h"

#include <vector>

namespace Equipment::RaceSwitchRestore {
struct PendingRestore {
    Core::ActorKey actor;
    RE::FormID raceFormID {0};
    Core::TargetAssignments assignments;
};

void BeginRaceSwitch(RE::Actor& a_actor, RE::TESRace& a_targetRace);
bool MarkClearedDuringRaceSwitch(RE::Actor& a_actor);
[[nodiscard]] bool HandleRaceSwitchComplete(RE::Actor& a_actor);
[[nodiscard]] std::vector<PendingRestore> GetPendingRestores();
void ReplacePendingRestores(std::vector<PendingRestore> a_restores);
void ClearActiveSwitches();
void Revert();
}
