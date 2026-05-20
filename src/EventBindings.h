#pragma once

#include <optional>
#include <vector>

#include "RuntimeClones.h"
#include "Serialization.h"

namespace EventBindings {
class ScopedMirror {
public:
    ScopedMirror(const RE::TESForm& a_source, const RE::TESForm& a_clone, bool a_adoptLoadedBinding = false);
    ScopedMirror(const ScopedMirror&) = delete;
    ScopedMirror(ScopedMirror&&) = delete;
    ScopedMirror& operator=(const ScopedMirror&) = delete;
    ScopedMirror& operator=(ScopedMirror&&) = delete;
    ~ScopedMirror();

private:
    bool active_ {false};
};

[[nodiscard]] bool BeginPendingMirror(
    const RE::TESForm& a_source,
    const RE::TESForm& a_clone,
    const RE::BSFixedString& a_eventName,
    bool a_adoptLoadedBinding = false,
    std::optional<RE::FormID> a_expectedActorFormID = std::nullopt
);
bool MirrorActiveTarget(
    RE::VMHandle a_handle,
    const RE::BSFixedString& a_eventName,
    std::optional<RE::FormID> a_actorFormID = std::nullopt
);
[[nodiscard]] bool MirrorScriptsAndDispatch(
    const RE::TESForm& a_source,
    RE::FormID a_cloneFormID,
    RE::VMHandle a_targetHandle,
    RE::Actor& a_actor,
    const RE::BSFixedString& a_eventName
);
void RemoveForClone(RE::FormID a_cloneFormID);
void RemoveForSource(RE::FormID a_sourceFormID);
void RemoveForSource(RE::FormID a_sourceFormID, RE::Actor& a_unequippedActor);
[[nodiscard]] bool ValidateLoadedRestore(const RE::TESForm& a_source, RE::FormID a_cloneFormID);
[[nodiscard]] bool AdoptLoadedBinding(const RE::TESForm& a_source, RE::FormID a_cloneFormID);
[[nodiscard]] bool HasLoadedBinding(RE::FormID a_sourceFormID, RE::FormID a_cloneFormID);
[[nodiscard]] std::optional<RE::VMHandle> GetHandle(RE::FormID a_sourceFormID, RE::FormID a_cloneFormID);
[[nodiscard]] bool ConsumeLoadedFailure(RE::FormID a_sourceFormID, RE::FormID a_cloneFormID);
void Save(SKSE::SerializationInterface& a_intfc, const std::vector<RuntimeClones::CloneKey>& a_selectedSources);
bool LoadRecord(Serialization::RecordInfo a_recordInfo, SKSE::SerializationInterface& a_intfc);
void Revert();
}
