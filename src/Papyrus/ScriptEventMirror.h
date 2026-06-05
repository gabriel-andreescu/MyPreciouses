#pragma once

#include <vector>

#include "Serialization.h"

namespace Papyrus::ScriptEventMirror {
struct BindingRetentionKey {
    RE::FormID sourceFormID {0};
    RE::FormID effectSourceFormID {0};
};

[[nodiscard]] bool DispatchEquipped(
    RE::Actor& a_actor,
    const RE::TESObjectARMO& a_source,
    RE::TESObjectARMO& a_effectSource
);
void RemoveEffectSourceBindings(RE::FormID a_effectSourceFormID);
void RemoveEffectSourceBindingsForUnequip(RE::FormID a_effectSourceFormID, RE::Actor& a_unequippedActor);
void SuspendEffectSourceBindingsForUnequip(RE::FormID a_effectSourceFormID, RE::Actor& a_unequippedActor);
[[nodiscard]] bool HasLoadedActiveBinding(RE::FormID a_sourceFormID, RE::FormID a_effectSourceFormID);
void SaveBindings(SKSE::SerializationInterface& a_intfc, const std::vector<BindingRetentionKey>& a_retainedBindings);
bool TryLoadBindingRecord(Serialization::RecordInfo a_recordInfo, SKSE::SerializationInterface& a_intfc);
void RevertBindings();
}
