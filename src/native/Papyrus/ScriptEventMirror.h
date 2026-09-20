#pragma once

#include <RE/Skyrim.h> // IWYU pragma: keep
#include <SKSE/SKSE.h> // IWYU pragma: keep

#include <vector>

#include "Core/ActorKey.h"
#include "Core/ItemSource.h"
#include "Serialization.h"

namespace Papyrus::ScriptEventMirror {
struct BindingRetentionKey {
    Core::ActorKey actor;
    RE::FormID sourceFormID {0};
    RE::FormID effectSourceFormID {0};
};

struct BindingSnapshot {
    Core::ActorKey actor;
    RE::FormID sourceFormID {0};
    RE::FormID effectSourceFormID {0};
    bool suspended {false};
};

[[nodiscard]] std::vector<BindingSnapshot> GetBindingSnapshots();
void HandleUniqueIDChange(const RE::TESUniqueIDChangeEvent& a_event);

[[nodiscard]] bool DispatchEquipped(
    RE::Actor& a_actor,
    const RE::TESObjectARMO& a_ring,
    const Core::ItemSource& a_source,
    RE::TESObjectARMO const& a_effectSource
);
void RemoveEffectSourceBindings(Core::ActorKey a_actor, RE::FormID a_effectSourceFormID);
void RemoveEffectSourceBindingsForUnequip(RE::FormID a_effectSourceFormID, RE::Actor& a_actor);
void SuspendEffectSourceBindingsForUnequip(RE::FormID a_effectSourceFormID, RE::Actor& a_actor);
[[nodiscard]] bool HasLoadedActiveBinding(
    Core::ActorKey a_actor,
    RE::FormID a_sourceFormID,
    RE::FormID a_effectSourceFormID
);
void SaveBindings(SKSE::SerializationInterface& a_intfc, const std::vector<BindingRetentionKey>& a_retainedBindings);
bool TryLoadBindingRecord(Serialization::RecordInfo a_recordInfo, SKSE::SerializationInterface& a_intfc);
void RevertBindings();
}
