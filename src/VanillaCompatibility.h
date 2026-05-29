#pragma once

#include <span>

namespace SKSE {
class SerializationInterface;
}

namespace Serialization {
struct RecordInfo;
}

namespace VanillaCompatibility {
void RefreshFrostmoonVirtualRings(std::span<const RE::FormID> a_virtualRingSourceFormIDs);
void OnPlayerSpellCast(RE::Actor& a_actor, RE::FormID a_spellFormID);
void OnPlayerRaceSwitchComplete(RE::Actor& a_actor);
void Save(SKSE::SerializationInterface& a_intfc);
[[nodiscard]] bool LoadRecord(Serialization::RecordInfo a_recordInfo, SKSE::SerializationInterface& a_intfc);
void Revert();
}
