#pragma once

#include <RE/Skyrim.h> // IWYU pragma: keep
#include <SKSE/SKSE.h> // IWYU pragma: keep

#include "Core/ActorKey.h"

#include <span>

namespace SKSE {
class SerializationInterface;
}

namespace Serialization {
struct RecordInfo;
}

namespace RE {
class TESFile;
}

namespace Compatibility::Vanilla {
[[nodiscard]] bool IsOfficialRingDefiningFile(const RE::TESFile* a_file);
void RefreshFrostmoonVirtualRings(Core::ActorKey a_actor, std::span<const RE::FormID> a_virtualRingSourceFormIDs);
void HandleSpellCast(RE::Actor& a_actor, RE::FormID a_spellFormID);
void HandleRaceSwitchComplete(RE::Actor& a_actor);
void Save(SKSE::SerializationInterface& a_intfc);
[[nodiscard]] bool TryLoadRecord(Serialization::RecordInfo a_recordInfo, SKSE::SerializationInterface& a_intfc);
void Revert();
}
