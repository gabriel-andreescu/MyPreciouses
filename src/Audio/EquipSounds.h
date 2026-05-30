#pragma once

#include <cstdint>
#include <memory>

namespace Audio::EquipSounds {
enum class Cue : std::uint8_t {
    kNone = 0,
    kEquip = 1,
    kUnequip = 2,
};

inline void Play(RE::Actor& a_actor, RE::TESObjectARMO& a_ring, const Cue a_cue) {
    if (a_cue == Cue::kNone) {
        return;
    }

    const auto playPickupSound = a_cue == Cue::kEquip;
    a_actor.PlayPickUpSound(std::addressof(a_ring), playPickupSound, false);
}

inline void Play(RE::Actor& a_actor, const RE::FormID a_sourceFormID, const Cue a_cue) {
    if (a_cue == Cue::kNone || a_sourceFormID == 0) {
        return;
    }

    auto* sourceRing = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_sourceFormID);
    if (!sourceRing) {
        return;
    }

    Play(a_actor, *sourceRing, a_cue);
}
}
