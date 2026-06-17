#pragma once

#include "Core/ActorKey.h"

#include <span>

namespace SKSE {
class SerializationInterface;
}

namespace Serialization {
struct RecordInfo;
}

namespace Compatibility::Vanilla {
[[nodiscard]] bool IsBondOfMatrimony(const RE::TESObjectARMO* a_ring);
void RefreshFrostmoonVirtualRings(Core::ActorKey a_actor, std::span<const RE::FormID> a_virtualRingSourceFormIDs);
void HandleSpellCast(RE::Actor& a_actor, RE::FormID a_spellFormID);
void HandleRaceSwitchComplete(RE::Actor& a_actor);
void Save(SKSE::SerializationInterface& a_intfc);
[[nodiscard]] bool TryLoadRecord(Serialization::RecordInfo a_recordInfo, SKSE::SerializationInterface& a_intfc);
void Revert();
}
