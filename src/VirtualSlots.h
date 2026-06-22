#pragma once

#include "Audio/EquipSounds.h"
#include "Core/ActorKey.h"
#include "Core/Target.h"
#include "Papyrus/ScriptEventMirror.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace VirtualSlots {
enum class ScriptBindingClearMode : std::uint8_t {
    kRelease = 0,
    kSuspend,
};

struct RefreshOptions {
    std::optional<Core::Target> soundTarget;
    Audio::EquipSounds::Cue sound {Audio::EquipSounds::Cue::kNone};
    bool preserveLoadedEffects {false};
    bool reapplyEffects {false};
};

void RequestRefresh(Core::ActorKey a_actor, RefreshOptions a_options = {});
void RequestVisualRefresh(Core::ActorKey a_actor);
void ClearTarget(
    Core::ActorKey a_actor,
    Core::Target a_target,
    Audio::EquipSounds::Cue a_sound = Audio::EquipSounds::Cue::kNone,
    ScriptBindingClearMode a_scriptBindings = ScriptBindingClearMode::kRelease
);
void Revert();

[[nodiscard]] bool MatchesGetEquippedCondition(RE::Actor& a_actor, RE::TESForm& a_getEquippedArgument);
[[nodiscard]] bool MatchesWornHasKeywordCondition(RE::Actor& a_actor, RE::BGSKeyword& a_wornHasKeywordArgument);
[[nodiscard]] float GetRingEnchantmentScaleForSource(RE::Actor& a_actor, const RE::TESObjectARMO* a_source);
[[nodiscard]] std::vector<Papyrus::ScriptEventMirror::BindingRetentionKey> GetActiveBindingRetentionKeys();
}
