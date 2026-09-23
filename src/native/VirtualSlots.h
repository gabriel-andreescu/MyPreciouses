#pragma once

#include <RE/Skyrim.h> // IWYU pragma: keep

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
    bool restoreMissingEffects {false};
};

void RequestRefresh(Core::ActorKey a_actor, RefreshOptions a_options = {});
void RequestVisualRefresh(Core::ActorKey a_actor);
void RemapUniqueID(const Core::ExtraUniqueIDKey& a_previous, const Core::ExtraUniqueIDKey& a_next);
void ClearTarget(
    Core::ActorKey a_actor,
    Core::Target a_target,
    Audio::EquipSounds::Cue a_sound = Audio::EquipSounds::Cue::kNone,
    ScriptBindingClearMode a_scriptBindings = ScriptBindingClearMode::kRelease
);
void Revert();

[[nodiscard]] bool MatchesGetEquippedCondition(RE::Actor const& a_actor, RE::TESForm& a_getEquippedArgument);
[[nodiscard]] bool MatchesWornHasKeywordCondition(RE::Actor const& a_actor, RE::BGSKeyword& a_wornHasKeywordArgument);
[[nodiscard]] float GetRingEnchantmentScaleForSource(RE::Actor& a_actor, const RE::TESObjectARMO* a_source);
[[nodiscard]] std::vector<Papyrus::ScriptEventMirror::BindingRetentionKey> GetActiveBindingRetentionKeys();
}
