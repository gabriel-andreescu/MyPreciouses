#include "Papyrus/ScriptEventMirror.h"

#include "Papyrus/FormScriptNames.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Papyrus::ScriptEventMirror {
namespace {
    constexpr auto kRecordPapyrusBindings = Serialization::MakeRecordType('P', 'Y', 'B', 'D');
    constexpr std::uint32_t kPapyrusBindingsVersion = 1;
    constexpr const char* kOnEquipped = "OnEquipped";
    constexpr const char* kOnUnequipped = "OnUnequipped";

    struct OwnedScriptObject {
        std::string scriptName;
        RE::BSTSmartPointer<RE::BSScript::Object> object;
    };

    struct BindingRecord {
        RE::FormID sourceFormID {0};
        RE::FormID effectSourceFormID {0};
        RE::VMHandle handle {0};
        bool loadedFromSave {false};
        std::vector<std::string> expectedScriptNames;
        std::vector<std::string> ownedScriptNames;
        std::vector<OwnedScriptObject> ownedScriptObjects;
    };

    struct StoredBindingHeader {
        RE::FormID sourceFormID {0};
        RE::FormID effectSourceFormID {0};
        RE::VMHandle handle {0};
        std::uint32_t expectedScriptCount {0};
        std::uint32_t ownedScriptCount {0};
    };

    [[nodiscard]] bool CanReadStoredScriptNames(const std::uint32_t a_remaining, const std::uint32_t a_scriptCount) {
        return a_scriptCount <= a_remaining / sizeof(std::uint32_t);
    }

    [[nodiscard]] bool ReadStoredScriptNames(
        SKSE::SerializationInterface& a_intfc,
        std::uint32_t& a_remaining,
        const std::uint32_t a_scriptCount,
        const std::uint32_t a_bindingIndex,
        const char* a_countOverflowReason,
        const char* a_readFailureReason,
        std::vector<RE::BSFixedString>& a_scriptNames
    ) {
        if (!CanReadStoredScriptNames(a_remaining, a_scriptCount)) {
            logger::error(
                "Papyrus: script event mirror load failed | index={} | reason={}",
                a_bindingIndex,
                a_countOverflowReason
            );
            return false;
        }

        a_scriptNames.reserve(a_scriptCount);
        for (std::uint32_t scriptIndex = 0; scriptIndex < a_scriptCount; ++scriptIndex) {
            const auto scriptName = Serialization::ReadString(a_intfc, a_remaining);
            if (!scriptName) {
                logger::error(
                    "Papyrus: script event mirror load failed | index={} | scriptIndex={} | reason={}",
                    a_bindingIndex,
                    scriptIndex,
                    a_readFailureReason
                );
                return false;
            }
            a_scriptNames.emplace_back(*scriptName);
        }

        return true;
    }

    struct BindingHandle {
        RE::FormID sourceFormID {0};
        RE::FormID effectSourceFormID {0};
        RE::VMHandle handle {0};
    };

    struct Registry {
        std::mutex lock;
        std::unordered_map<RE::FormID, std::vector<BindingRecord>> byEffectSource;
    };

    [[nodiscard]] Registry& GetRegistry() {
        static Registry registry;
        return registry;
    }

    [[nodiscard]] RE::VMHandle GetVmHandleForForm(const RE::TESForm& a_form) {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        auto* handlePolicy = vm ? vm->GetObjectHandlePolicy() : nullptr;
        if (!handlePolicy) {
            return 0;
        }

        const auto handle = handlePolicy->GetHandleForObject(a_form.GetFormType(), std::addressof(a_form));
        return handle != handlePolicy->EmptyHandle() ? handle : 0;
    }

    [[nodiscard]] bool ContainsScriptName(
        const std::vector<RE::BSFixedString>& a_scriptNames,
        const RE::BSFixedString& a_scriptName
    ) {
        return std::ranges::find(a_scriptNames, a_scriptName) != a_scriptNames.end();
    }

    [[nodiscard]] std::vector<std::string> ToStoredScriptNames(const std::vector<RE::BSFixedString>& a_scriptNames) {
        std::vector<std::string> storedNames;
        storedNames.reserve(a_scriptNames.size());
        for (const auto& scriptName : a_scriptNames) {
            storedNames.emplace_back(scriptName.c_str());
        }
        return storedNames;
    }

    [[nodiscard]] bool HasBindingForHandle(const RE::FormID a_effectSourceFormID, const RE::VMHandle a_handle) {
        auto& registry = GetRegistry();
        std::scoped_lock lock(registry.lock);
        const auto it = registry.byEffectSource.find(a_effectSourceFormID);
        return it
               != registry.byEffectSource.end()
               && std::ranges::any_of(it->second, [a_handle](const auto& a_binding) {
                      return a_binding.handle == a_handle;
                  });
    }

    [[nodiscard]] auto FindBindingBySource(std::vector<BindingRecord>& a_bindings, const RE::FormID a_sourceFormID) {
        return std::ranges::find_if(a_bindings, [a_sourceFormID](const auto& a_binding) {
            return a_binding.sourceFormID == a_sourceFormID;
        });
    }

    void UpsertOwnedScriptObject(
        BindingRecord& a_binding,
        std::string a_scriptName,
        RE::BSTSmartPointer<RE::BSScript::Object> a_object
    ) {
        if (a_scriptName.empty() || !a_object) {
            return;
        }

        auto it = std::ranges::find_if(a_binding.ownedScriptObjects, [&](const auto& a_entry) {
            return a_entry.scriptName == a_scriptName;
        });
        if (it == a_binding.ownedScriptObjects.end()) {
            a_binding.ownedScriptObjects.push_back(
                OwnedScriptObject {
                    .scriptName = std::move(a_scriptName),
                    .object = std::move(a_object),
                }
            );
            return;
        }

        it->object = std::move(a_object);
    }

    void MergeOwnedScriptObjects(BindingRecord& a_binding, const std::vector<OwnedScriptObject>& a_ownedScriptObjects) {
        for (const auto& ownedScriptObject : a_ownedScriptObjects) {
            UpsertOwnedScriptObject(a_binding, ownedScriptObject.scriptName, ownedScriptObject.object);
        }
    }

    void RecordBinding(
        const RE::FormID a_sourceFormID,
        const RE::FormID a_effectSourceFormID,
        const RE::VMHandle a_handle,
        const std::vector<RE::BSFixedString>& a_expectedScriptNames,
        const std::vector<RE::BSFixedString>& a_ownedScriptNames,
        const std::vector<OwnedScriptObject>& a_ownedScriptObjects,
        const bool a_loadedFromSave
    ) {
        auto& registry = GetRegistry();
        std::scoped_lock lock(registry.lock);
        auto& bindings = registry.byEffectSource[a_effectSourceFormID];
        auto it = FindBindingBySource(bindings, a_sourceFormID);
        const auto expectedScriptNames = ToStoredScriptNames(a_expectedScriptNames);
        const auto ownedScriptNames = ToStoredScriptNames(a_ownedScriptNames);

        if (it == bindings.end()) {
            bindings.push_back(
                BindingRecord {
                    .sourceFormID = a_sourceFormID,
                    .effectSourceFormID = a_effectSourceFormID,
                    .handle = a_handle,
                    .loadedFromSave = a_loadedFromSave,
                    .expectedScriptNames = expectedScriptNames,
                    .ownedScriptNames = ownedScriptNames,
                }
            );
            MergeOwnedScriptObjects(bindings.back(), a_ownedScriptObjects);
            return;
        }

        it->sourceFormID = a_sourceFormID;
        it->effectSourceFormID = a_effectSourceFormID;
        it->handle = a_handle;
        it->loadedFromSave = a_loadedFromSave;
        if (!expectedScriptNames.empty()) {
            it->expectedScriptNames = expectedScriptNames;
        }
        for (const auto& ownedScriptName : ownedScriptNames) {
            if (std::ranges::find(it->ownedScriptNames, ownedScriptName) == it->ownedScriptNames.end()) {
                it->ownedScriptNames.push_back(ownedScriptName);
            }
        }
        MergeOwnedScriptObjects(*it, a_ownedScriptObjects);
    }

    [[nodiscard]] std::optional<BindingRecord> FindBindingRecord(
        const RE::FormID a_sourceFormID,
        const RE::FormID a_effectSourceFormID,
        const bool a_requireLoadedFromSave
    ) {
        auto& registry = GetRegistry();
        std::scoped_lock lock(registry.lock);
        const auto effectSourceIt = registry.byEffectSource.find(a_effectSourceFormID);
        if (effectSourceIt == registry.byEffectSource.end()) {
            return std::nullopt;
        }

        const auto bindingIt = std::ranges::find_if(effectSourceIt->second, [&](const auto& a_binding) {
            return a_binding.sourceFormID == a_sourceFormID && (!a_requireLoadedFromSave || a_binding.loadedFromSave);
        });
        if (bindingIt == effectSourceIt->second.end()) {
            return std::nullopt;
        }

        return *bindingIt;
    }

    void MarkBindingAdopted(
        const BindingHandle& a_bindingHandle,
        const std::vector<OwnedScriptObject>& a_ownedScriptObjects
    ) {
        auto& registry = GetRegistry();
        std::scoped_lock lock(registry.lock);
        const auto effectSourceIt = registry.byEffectSource.find(a_bindingHandle.effectSourceFormID);
        if (effectSourceIt == registry.byEffectSource.end()) {
            return;
        }

        auto bindingIt = FindBindingBySource(effectSourceIt->second, a_bindingHandle.sourceFormID);
        if (bindingIt == effectSourceIt->second.end()) {
            return;
        }

        bindingIt->handle = a_bindingHandle.handle;
        bindingIt->loadedFromSave = false;
        MergeOwnedScriptObjects(*bindingIt, a_ownedScriptObjects);
    }

    [[nodiscard]] std::vector<RE::BSFixedString> GetAttachedScriptNames(
        RE::BSScript::Internal::VirtualMachine& a_vm,
        const RE::VMHandle a_handle
    ) {
        std::vector<RE::BSFixedString> scriptNames;

        RE::BSSpinLockGuard lock {a_vm.attachedScriptsLock};
        const auto it = a_vm.attachedScripts.find(a_handle);
        if (it == a_vm.attachedScripts.end()) {
            return scriptNames;
        }

        for (const auto& attachedScript : it->second) {
            const auto* object = attachedScript.get();
            const auto* type = object ? object->GetTypeInfo() : nullptr;
            const auto* name = type ? type->GetName() : nullptr;
            if (!name || name[0] == '\0') {
                continue;
            }

            const RE::BSFixedString scriptName {name};
            if (!ContainsScriptName(scriptNames, scriptName)) {
                scriptNames.push_back(scriptName);
            }
        }

        return scriptNames;
    }

    [[nodiscard]] bool ScriptTypeMatches(const RE::BSScript::Object& a_object, const std::string& a_scriptName) {
        for (const auto* type = a_object.GetTypeInfo(); type; type = type->GetParent()) {
            const auto* typeName = type->GetName();
            if (typeName && _stricmp(typeName, a_scriptName.c_str()) == 0) {
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] RE::BSTSmartPointer<RE::BSScript::Object> FindAttachedScriptObject(
        RE::BSScript::Internal::VirtualMachine& a_vm,
        const RE::VMHandle a_handle,
        const std::string& a_scriptName
    ) {
        RE::BSTSmartPointer<RE::BSScript::Object> result;

        RE::BSSpinLockGuard lock {a_vm.attachedScriptsLock};
        const auto it = a_vm.attachedScripts.find(a_handle);
        if (it == a_vm.attachedScripts.end()) {
            return result;
        }

        for (const auto& attachedScript : it->second) {
            RE::BSTSmartPointer<RE::BSScript::Object> object {attachedScript.get()};
            if (object && ScriptTypeMatches(*object, a_scriptName)) {
                result = object;
                break;
            }
        }

        return result;
    }

    [[nodiscard]] RE::BSTSmartPointer<RE::BSScript::Object> FindOwnedScriptObject(
        const BindingRecord& a_binding,
        const std::string& a_scriptName
    ) {
        const auto it = std::ranges::find_if(a_binding.ownedScriptObjects, [&](const auto& a_entry) {
            return a_entry.scriptName == a_scriptName;
        });

        return it != a_binding.ownedScriptObjects.end() ? it->object : nullptr;
    }

    [[nodiscard]] bool ResolveExpectedScriptObjects(
        RE::BSScript::Internal::VirtualMachine& a_vm,
        BindingRecord& a_binding,
        const RE::VMHandle a_handle
    ) {
        for (const auto& scriptName : a_binding.expectedScriptNames) {
            if (scriptName.empty()) {
                return false;
            }

            auto object = FindAttachedScriptObject(a_vm, a_handle, scriptName);
            if (!object) {
                return false;
            }

            if (std::ranges::find(a_binding.ownedScriptNames, scriptName) != a_binding.ownedScriptNames.end()) {
                UpsertOwnedScriptObject(a_binding, scriptName, object);
            }
        }

        return true;
    }

    std::uint32_t UnbindOwnedScripts(RE::BSScript::Internal::VirtualMachine& a_vm, const BindingRecord& a_binding) {
        auto* bindPolicy = a_vm.GetObjectBindPolicy();
        auto* bindInterface = bindPolicy ? bindPolicy->bindInterface : nullptr;
        if (!bindInterface) {
            return 0;
        }

        auto unboundCount = std::uint32_t {0};
        for (const auto& scriptName : a_binding.ownedScriptNames) {
            if (scriptName.empty()) {
                continue;
            }

            auto object = FindOwnedScriptObject(a_binding, scriptName);
            if (!object) {
                continue;
            }

            const auto boundHandle = bindInterface->GetBoundHandle(object);
            auto* handlePolicy = a_vm.GetObjectHandlePolicy();
            if (handlePolicy && boundHandle == handlePolicy->EmptyHandle()) {
                continue;
            }

            bindInterface->UnbindObject(object);
            ++unboundCount;
        }

        return unboundCount;
    }

    std::uint32_t DispatchOwnedEventScripts(
        RE::BSScript::Internal::VirtualMachine& a_vm,
        const BindingRecord& a_binding,
        RE::Actor& a_actor,
        const RE::BSFixedString& a_eventName
    ) {
        auto dispatchedCount = std::uint32_t {0};
        for (const auto& scriptName : a_binding.ownedScriptNames) {
            if (scriptName.empty()) {
                continue;
            }

            auto object = FindOwnedScriptObject(a_binding, scriptName);
            if (!object) {
                continue;
            }

            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
            auto* args = RE::MakeFunctionArguments(std::addressof(a_actor));
            const auto dispatched = a_vm.DispatchMethodCall(object, a_eventName, args, callback);
            delete args;

            if (!dispatched) {
                continue;
            }

            ++dispatchedCount;
        }

        return dispatchedCount;
    }

    bool SetInitialProperties(
        RE::BSScript::Internal::VirtualMachine& a_vm,
        RE::BSTSmartPointer<RE::BSScript::Object>& a_object,
        const RE::BSTScrapHashMap<RE::BSFixedString, RE::BSScript::Variable>& a_properties
    ) {
        for (const auto& [propertyName, propertyValue] : a_properties) {
            auto value = propertyValue;
            if (!a_vm.SetPropertyValue(a_object, propertyName.c_str(), value)) {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] bool MirrorScriptsToEffectSourceHandle(
        const RE::TESForm& a_source,
        const RE::FormID a_effectSourceFormID,
        const RE::VMHandle a_effectSourceHandle
    ) {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            return false;
        }

        auto* handlePolicy = vm->GetObjectHandlePolicy();
        auto* bindPolicy = vm->GetObjectBindPolicy();
        if (!handlePolicy || !bindPolicy) {
            return false;
        }

        if (a_effectSourceHandle == handlePolicy->EmptyHandle()) {
            return false;
        }

        const auto sourceHandle = handlePolicy->GetHandleForObject(a_source.GetFormType(), std::addressof(a_source));
        if (sourceHandle == handlePolicy->EmptyHandle()) {
            return false;
        }

        const auto recordScriptNames = FormScriptNames::GetRecordScriptNames(a_source);
        if (recordScriptNames.empty()) {
            return false;
        }

        const auto existingScripts = GetAttachedScriptNames(*vm, a_effectSourceHandle);
        std::vector<RE::BSFixedString> ownedScriptNames;
        std::vector<OwnedScriptObject> ownedScriptObjects;
        auto mirroredCount = std::uint32_t {0};
        for (const auto& scriptName : recordScriptNames) {
            if (ContainsScriptName(existingScripts, scriptName)) {
                continue;
            }

            RE::BSTScrapHashMap<RE::BSFixedString, RE::BSScript::Variable> sourceProperties;
            std::uint32_t sourceNonConverted = 0;
            bindPolicy->GetInitialPropertyValues(sourceHandle, scriptName, sourceProperties, sourceNonConverted);

            RE::BSTSmartPointer<RE::BSScript::Object> scriptObject;
            if (!vm->CreateObject(scriptName, scriptObject) || !scriptObject) {
                continue;
            }

            if (!SetInitialProperties(*vm, scriptObject, sourceProperties)) {
                continue;
            }

            bindPolicy->BindObject(scriptObject, a_effectSourceHandle);
            ownedScriptNames.push_back(scriptName);
            ownedScriptObjects.push_back(
                OwnedScriptObject {
                    .scriptName = scriptName.c_str(),
                    .object = scriptObject,
                }
            );
            ++mirroredCount;
        }

        if (mirroredCount > 0 || HasBindingForHandle(a_effectSourceFormID, a_effectSourceHandle)) {
            RecordBinding(
                a_source.GetFormID(),
                a_effectSourceFormID,
                a_effectSourceHandle,
                recordScriptNames,
                ownedScriptNames,
                ownedScriptObjects,
                false
            );
        }

        return mirroredCount > 0 || HasBindingForHandle(a_effectSourceFormID, a_effectSourceHandle);
    }

    [[nodiscard]] bool DispatchMirroredScriptsForEvent(
        const RE::TESForm& a_source,
        const RE::FormID a_effectSourceFormID,
        RE::Actor& a_actor,
        const RE::BSFixedString& a_eventName
    ) {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            return false;
        }

        auto binding = FindBindingRecord(a_source.GetFormID(), a_effectSourceFormID, false);
        if (!binding) {
            return false;
        }

        const auto dispatched = DispatchOwnedEventScripts(*vm, *binding, a_actor, a_eventName);
        return dispatched > 0;
    }

    [[nodiscard]] std::vector<BindingRecord> CopyBindingsForEffectSource(const RE::FormID a_effectSourceFormID) {
        auto& registry = GetRegistry();
        std::scoped_lock lock(registry.lock);
        const auto it = registry.byEffectSource.find(a_effectSourceFormID);
        return it != registry.byEffectSource.end() ? it->second : std::vector<BindingRecord> {};
    }

    void EraseBindingsForEffectSource(const RE::FormID a_effectSourceFormID) {
        auto& registry = GetRegistry();
        std::scoped_lock lock(registry.lock);
        registry.byEffectSource.erase(a_effectSourceFormID);
    }

    void ReleaseEffectSourceBindings(std::vector<BindingRecord>& a_bindings, RE::Actor* a_unequippedActor) {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            return;
        }

        for (auto& binding : a_bindings) {
            static_cast<void>(ResolveExpectedScriptObjects(*vm, binding, binding.handle));
            if (a_unequippedActor) {
                static_cast<void>(
                    DispatchOwnedEventScripts(*vm, binding, *a_unequippedActor, RE::BSFixedString {kOnUnequipped})
                );
            }
            static_cast<void>(UnbindOwnedScripts(*vm, binding));
        }
    }

    [[nodiscard]] bool AdoptRestorableBinding(const RE::TESForm& a_source, const RE::FormID a_effectSourceFormID) {
        auto binding = FindBindingRecord(a_source.GetFormID(), a_effectSourceFormID, true);
        if (!binding) {
            return false;
        }

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            return false;
        }

        if (!ResolveExpectedScriptObjects(*vm, *binding, binding->handle)) {
            static_cast<void>(UnbindOwnedScripts(*vm, *binding));
            return false;
        }

        MarkBindingAdopted(
            BindingHandle {
                .sourceFormID = a_source.GetFormID(),
                .effectSourceFormID = a_effectSourceFormID,
                .handle = binding->handle,
            },
            binding->ownedScriptObjects
        );
        return true;
    }
}

bool DispatchEquipped(RE::Actor& a_actor, const RE::TESObjectARMO& a_source, RE::TESObjectARMO& a_effectSource) {
    const auto effectSourceFormID = a_effectSource.GetFormID();
    if (AdoptRestorableBinding(a_source, effectSourceFormID)) {
        return true;
    }

    const auto handle = GetVmHandleForForm(a_effectSource);
    if (handle == 0) {
        return false;
    }

    const auto mirrored = MirrorScriptsToEffectSourceHandle(a_source, effectSourceFormID, handle);
    if (!mirrored) {
        return false;
    }

    return DispatchMirroredScriptsForEvent(a_source, effectSourceFormID, a_actor, RE::BSFixedString {kOnEquipped});
}

void RemoveEffectSourceBindings(const RE::FormID a_effectSourceFormID) {
    auto bindings = CopyBindingsForEffectSource(a_effectSourceFormID);
    ReleaseEffectSourceBindings(bindings, nullptr);
    EraseBindingsForEffectSource(a_effectSourceFormID);
}

void RemoveEffectSourceBindingsForUnequip(const RE::FormID a_effectSourceFormID, RE::Actor& a_unequippedActor) {
    auto bindings = CopyBindingsForEffectSource(a_effectSourceFormID);
    ReleaseEffectSourceBindings(bindings, std::addressof(a_unequippedActor));
    EraseBindingsForEffectSource(a_effectSourceFormID);
}

bool HasRestorableBinding(const RE::FormID a_sourceFormID, const RE::FormID a_effectSourceFormID) {
    return FindBindingRecord(a_sourceFormID, a_effectSourceFormID, true).has_value();
}

void SaveBindings(SKSE::SerializationInterface& a_intfc, const std::vector<BindingRetentionKey>& a_retainedBindings) {
    if (a_retainedBindings.empty()) {
        return;
    }

    std::vector<BindingRecord> bindings;
    {
        auto& registry = GetRegistry();
        std::scoped_lock lock(registry.lock);
        for (const auto& effectSourceBindings : registry.byEffectSource | std::views::values) {
            for (const auto& binding : effectSourceBindings) {
                const auto retained = std::ranges::any_of(a_retainedBindings, [&](const auto& a_retainedBinding) {
                    return a_retainedBinding.sourceFormID
                           == binding.sourceFormID
                           && a_retainedBinding.effectSourceFormID
                           == binding.effectSourceFormID;
                });
                if (retained) {
                    bindings.push_back(binding);
                }
            }
        }
    }

    if (bindings.empty()) {
        return;
    }

    if (!a_intfc.OpenRecord(kRecordPapyrusBindings, kPapyrusBindingsVersion)) {
        logger::error("Papyrus: script event mirror save failed | reason=openRecord");
        return;
    }

    const auto bindingCount = static_cast<std::uint32_t>(bindings.size());
    if (!Serialization::WriteField(a_intfc, bindingCount)) {
        logger::error("Papyrus: script event mirror save failed | reason=writeCount");
        return;
    }

    for (const auto& binding : bindings) {
        const StoredBindingHeader header {
            .sourceFormID = binding.sourceFormID,
            .effectSourceFormID = binding.effectSourceFormID,
            .handle = binding.handle,
            .expectedScriptCount = static_cast<std::uint32_t>(binding.expectedScriptNames.size()),
            .ownedScriptCount = static_cast<std::uint32_t>(binding.ownedScriptNames.size()),
        };

        if (!Serialization::WriteField(a_intfc, header)) {
            logger::error(
                "Papyrus: script event mirror save failed | source={:08X} | effectSource={:08X} | handle={:016X} | reason=writeHeader",
                binding.sourceFormID,
                binding.effectSourceFormID,
                binding.handle
            );
            return;
        }

        for (const auto& scriptName : binding.expectedScriptNames) {
            if (!Serialization::WriteString(a_intfc, scriptName)) {
                logger::error(
                    "Papyrus: script event mirror save failed | source={:08X} | effectSource={:08X} | handle={:016X} | script={} | reason=writeExpectedScript",
                    binding.sourceFormID,
                    binding.effectSourceFormID,
                    binding.handle,
                    scriptName
                );
                return;
            }
        }

        for (const auto& scriptName : binding.ownedScriptNames) {
            if (!Serialization::WriteString(a_intfc, scriptName)) {
                logger::error(
                    "Papyrus: script event mirror save failed | source={:08X} | effectSource={:08X} | handle={:016X} | script={} | reason=writeOwnedScript",
                    binding.sourceFormID,
                    binding.effectSourceFormID,
                    binding.handle,
                    scriptName
                );
                return;
            }
        }
    }
}

bool TryLoadBindingRecord(const Serialization::RecordInfo a_recordInfo, SKSE::SerializationInterface& a_intfc) {
    if (a_recordInfo.type != kRecordPapyrusBindings) {
        return false;
    }

    auto remaining = a_recordInfo.length;
    const auto drainRemaining = [&] {
        Serialization::DrainRecordData(a_intfc, remaining);
        remaining = 0;
    };

    if (a_recordInfo.version != kPapyrusBindingsVersion) {
        logger::warn(
            "Papyrus: script event mirror load skipped | version={} | reason=unsupportedVersion",
            a_recordInfo.version
        );
        drainRemaining();
        return true;
    }

    std::uint32_t bindingCount = 0;
    if (!Serialization::ReadField(a_intfc, remaining, bindingCount)) {
        logger::error("Papyrus: script event mirror load failed | reason=readCount");
        drainRemaining();
        return true;
    }

    for (std::uint32_t index = 0; index < bindingCount; ++index) {
        StoredBindingHeader stored;
        if (!Serialization::ReadField(a_intfc, remaining, stored)) {
            logger::error("Papyrus: script event mirror load failed | index={} | reason=readHeader", index);
            drainRemaining();
            return true;
        }

        std::vector<RE::BSFixedString> expectedScriptNames;
        if (!ReadStoredScriptNames(
                a_intfc,
                remaining,
                stored.expectedScriptCount,
                index,
                "expectedScriptCountOverflow",
                "readExpectedScript",
                expectedScriptNames
            )) {
            drainRemaining();
            return true;
        }

        std::vector<RE::BSFixedString> ownedScriptNames;
        if (!ReadStoredScriptNames(
                a_intfc,
                remaining,
                stored.ownedScriptCount,
                index,
                "ownedScriptCountOverflow",
                "readOwnedScript",
                ownedScriptNames
            )) {
            drainRemaining();
            return true;
        }

        RE::FormID resolvedSourceFormID = 0;
        RE::FormID resolvedEffectSourceFormID = 0;
        RE::VMHandle resolvedHandle = 0;
        const auto sourceResolved = stored.sourceFormID
                                    != 0
                                    && a_intfc.ResolveFormID(stored.sourceFormID, resolvedSourceFormID);
        const auto effectSourceResolved = stored.effectSourceFormID
                                          != 0
                                          && a_intfc.ResolveFormID(
                                              stored.effectSourceFormID,
                                              resolvedEffectSourceFormID
                                          );
        const auto handleResolved = stored.handle != 0 && a_intfc.ResolveHandle(stored.handle, resolvedHandle);

        if (!sourceResolved || !effectSourceResolved || !handleResolved) {
            logger::warn(
                "Papyrus: script event mirror entry skipped | index={} | source={:08X} | effectSource={:08X} | handle={:016X} | sourceResolved={} | effectSourceResolved={} | handleResolved={} | reason=resolveFailed",
                index,
                stored.sourceFormID,
                stored.effectSourceFormID,
                stored.handle,
                sourceResolved,
                effectSourceResolved,
                handleResolved
            );
            continue;
        }

        RecordBinding(
            resolvedSourceFormID,
            resolvedEffectSourceFormID,
            resolvedHandle,
            expectedScriptNames,
            ownedScriptNames,
            {},
            true
        );
    }

    drainRemaining();
    return true;
}

void RevertBindings() {
    std::vector<RE::FormID> effectSourceFormIDs;
    {
        auto& registry = GetRegistry();
        std::scoped_lock lock(registry.lock);
        for (const auto effectSourceFormID : registry.byEffectSource | std::views::keys) {
            effectSourceFormIDs.push_back(effectSourceFormID);
        }
    }

    for (const auto effectSourceFormID : effectSourceFormIDs) {
        RemoveEffectSourceBindings(effectSourceFormID);
    }
}
}
