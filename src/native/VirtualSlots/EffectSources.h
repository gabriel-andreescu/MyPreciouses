#pragma once

#include <RE/Skyrim.h> // IWYU pragma: keep
#include <SKSE/SKSE.h> // IWYU pragma: keep

#include "Core/ItemSource.h"
#include "Serialization.h"
#include "Settings.h"

#include <span>

namespace VirtualSlots::EffectSources {
[[nodiscard]] bool Matches(RE::FormID a_effectSource, const Core::ItemSource& a_source, ExtraRingMode a_mode);
[[nodiscard]] RE::TESObjectARMO* Acquire(
    RE::TESObjectARMO& a_ring,
    const Core::ItemSource& a_source,
    ExtraRingMode a_mode,
    std::span<const RE::FormID> a_usedByActor,
    RE::FormID a_retainedSource
);
void Save(SKSE::SerializationInterface& a_intfc);
bool TryLoadRecord(Serialization::RecordInfo a_record, SKSE::SerializationInterface& a_intfc);
void Revert();
}
