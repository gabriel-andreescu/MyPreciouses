#include "EventBindings.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace EventBindings {
namespace {
    class VmadReader {
    public:
        explicit VmadReader(const std::vector<std::uint8_t>& a_data)
            : data_(a_data) {}

        template <class T>
        [[nodiscard]] bool Read(T& a_out) {
            if (offset_ + sizeof(T) > data_.size()) {
                return false;
            }

            std::memcpy(std::addressof(a_out), data_.data() + offset_, sizeof(T));
            offset_ += sizeof(T);
            return true;
        }

        [[nodiscard]] std::optional<std::string> ReadWString() {
            std::uint16_t length = 0;
            if (!Read(length) || offset_ + length > data_.size()) {
                return std::nullopt;
            }

            std::string value {reinterpret_cast<const char*>(data_.data() + offset_), length};
            offset_ += length;
            if (!value.empty() && value.back() == '\0') {
                value.pop_back();
            }

            return value;
        }

        [[nodiscard]] bool Skip(const std::size_t a_bytes) {
            if (offset_ + a_bytes > data_.size()) {
                return false;
            }

            offset_ += a_bytes;
            return true;
        }

    private:
        const std::vector<std::uint8_t>& data_;
        std::size_t offset_ {0};
    };

    struct ScopedTESFile {
        explicit ScopedTESFile(RE::TESFile* a_file)
            : file(a_file) {}

        ScopedTESFile(const ScopedTESFile&) = delete;
        ScopedTESFile(ScopedTESFile&&) = delete;
        ScopedTESFile& operator=(const ScopedTESFile&) = delete;
        ScopedTESFile& operator=(ScopedTESFile&&) = delete;

        ~ScopedTESFile() {
            if (file) {
                file->CloseTES(true);
            }
        }

        RE::TESFile* file {nullptr};
    };

    [[nodiscard]] constexpr std::uint32_t MakeSubrecordType(
        const char a_first,
        const char a_second,
        const char a_third,
        const char a_fourth
    ) {
        return static_cast<std::uint8_t>(a_first)
               | (static_cast<std::uint8_t>(a_second) << 8)
               | (static_cast<std::uint8_t>(a_third) << 16)
               | (static_cast<std::uint8_t>(a_fourth) << 24);
    }

    [[nodiscard]] bool ContainsVmadScriptName(
        const std::vector<RE::BSFixedString>& a_scriptNames,
        const RE::BSFixedString& a_scriptName
    ) {
        return std::ranges::find(a_scriptNames, a_scriptName) != a_scriptNames.end();
    }
    [[nodiscard]] bool SkipVmadPropertyValue(
        VmadReader& a_reader,
        std::uint8_t a_propertyType,
        std::uint16_t a_objFormat
    );

    [[nodiscard]] bool SkipVmadSinglePropertyValue(
        VmadReader& a_reader,
        const std::uint8_t a_propertyType,
        [[maybe_unused]] const std::uint16_t a_objFormat
    ) {
        switch (a_propertyType) {
            case 1: return a_reader.Skip(8);
            case 2: return a_reader.ReadWString().has_value();
            case 3:
            case 4: return a_reader.Skip(4);
            case 5: return a_reader.Skip(1);
            default:
                if (a_propertyType >= 11 && a_propertyType <= 15) {
                    return SkipVmadPropertyValue(a_reader, a_propertyType, a_objFormat);
                }
                return false;
        }
    }

    [[nodiscard]] bool SkipVmadPropertyValue(
        VmadReader& a_reader,
        const std::uint8_t a_propertyType,
        const std::uint16_t a_objFormat
    ) {
        if (a_propertyType < 11 || a_propertyType > 15) {
            return SkipVmadSinglePropertyValue(a_reader, a_propertyType, a_objFormat);
        }

        std::uint32_t itemCount = 0;
        if (!a_reader.Read(itemCount)) {
            return false;
        }

        const auto elementType = static_cast<std::uint8_t>(a_propertyType - 10);
        for (std::uint32_t index = 0; index < itemCount; ++index) {
            if (!SkipVmadSinglePropertyValue(a_reader, elementType, a_objFormat)) {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] std::vector<RE::BSFixedString> ParsePrimaryVmadScriptNames(
        const std::vector<std::uint8_t>& a_data,
        const RE::TESForm& a_form
    ) {
        std::vector<RE::BSFixedString> scriptNames;
        VmadReader reader {a_data};

        std::uint16_t version = 0;
        std::uint16_t objFormat = 0;
        std::uint16_t scriptCount = 0;
        if (!reader.Read(version) || !reader.Read(objFormat) || !reader.Read(scriptCount)) {
            logger::warn("Papyrus: VMAD parse failed | form={:08X} | reason=header", a_form.GetFormID());
            return scriptNames;
        }

        for (std::uint16_t scriptIndex = 0; scriptIndex < scriptCount; ++scriptIndex) {
            const auto scriptName = reader.ReadWString();
            if (!scriptName) {
                logger::warn(
                    "Papyrus: VMAD parse failed | form={:08X} | scriptIndex={} | reason=scriptName",
                    a_form.GetFormID(),
                    scriptIndex
                );
                return scriptNames;
            }

            std::uint8_t scriptStatus = 0;
            if (version >= 4 && !reader.Read(scriptStatus)) {
                logger::warn(
                    "Papyrus: VMAD parse failed | form={:08X} | script={} | reason=scriptStatus",
                    a_form.GetFormID(),
                    scriptName->c_str()
                );
                return scriptNames;
            }

            std::uint16_t propertyCount = 0;
            if (!reader.Read(propertyCount)) {
                logger::warn(
                    "Papyrus: VMAD parse failed | form={:08X} | script={} | reason=propertyCount",
                    a_form.GetFormID(),
                    scriptName->c_str()
                );
                return scriptNames;
            }

            if (scriptStatus != 3) {
                const RE::BSFixedString fixedName {*scriptName};
                if (!fixedName.empty() && !ContainsVmadScriptName(scriptNames, fixedName)) {
                    scriptNames.push_back(fixedName);
                }
            }

            for (std::uint16_t propertyIndex = 0; propertyIndex < propertyCount; ++propertyIndex) {
                const auto propertyName = reader.ReadWString();
                std::uint8_t propertyType = 0;
                std::uint8_t propertyStatus = 0;
                if (!propertyName
                    || !reader.Read(propertyType)
                    || (version >= 4 && !reader.Read(propertyStatus))
                    || !SkipVmadPropertyValue(reader, propertyType, objFormat)) {
                    logger::warn(
                        "Papyrus: VMAD parse failed | form={:08X} | script={} | propertyIndex={} | reason=property",
                        a_form.GetFormID(),
                        scriptName->c_str(),
                        propertyIndex
                    );
                    return scriptNames;
                }
            }
        }

        return scriptNames;
    }

    [[nodiscard]] std::vector<RE::BSFixedString> GetPrimaryVmadScriptNames(const RE::TESForm& a_form) {
        constexpr auto kVMAD = MakeSubrecordType('V', 'M', 'A', 'D');
        std::vector<RE::BSFixedString> scriptNames;

        auto* sourceFile = a_form.GetFile();
        if (!sourceFile) {
            return scriptNames;
        }

        ScopedTESFile file {sourceFile->Duplicate()};
        if (!file.file) {
            logger::warn(
                "Papyrus: VMAD scripts unavailable | form={:08X} | file={} | reason=duplicateFailed",
                a_form.GetFormID(),
                sourceFile->GetFilename()
            );
            return scriptNames;
        }

        auto* mutableForm = const_cast<RE::TESForm*>(std::addressof(a_form));
        if (!file.file->SeekForm(mutableForm) || !file.file->SeekNextSubrecordType(kVMAD)) {
            return scriptNames;
        }

        const auto size = file.file->GetCurrentSubRecordSize();
        if (size == 0) {
            return scriptNames;
        }

        std::vector<std::uint8_t> data(size);
        if (!file.file->ReadData(data.data(), size)) {
            logger::warn(
                "Papyrus: VMAD scripts unavailable | form={:08X} | file={} | reason=readFailed",
                a_form.GetFormID(),
                sourceFile->GetFilename()
            );
            return scriptNames;
        }

        return ParsePrimaryVmadScriptNames(data, a_form);
    }

    constexpr const char* kOnEquipped = "OnEquipped";
    constexpr const char* kOnUnequipped = "OnUnequipped";
    constexpr auto kRecordPapyrusBindings = Serialization::MakeRecordType('P', 'Y', 'B', 'D');
    constexpr std::uint32_t kPapyrusBindingsVersion = 1;
    constexpr std::chrono::milliseconds kPendingMirrorLifetime {5000};

    struct EventMirrorContext {
        RE::FormID sourceFormID {0};
        RE::FormID effectSourceFormID {0};
        RE::BSFixedString eventName;
        std::optional<RE::FormID> expectedActorFormID;
        bool adoptLoadedBinding {false};
    };

    struct OwnedScriptObject {
        std::string scriptName;
        RE::BSTSmartPointer<RE::BSScript::Object> object;
    };

    struct BindingRecord {
        RE::FormID sourceFormID {0};
        RE::FormID effectSourceFormID {0};
        RE::VMHandle handle {0};
        bool loadedFromSave {false};
        bool adoptedFromSave {false};
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

    struct BindingHandle {
        RE::FormID sourceFormID {0};
        RE::FormID effectSourceFormID {0};
        RE::VMHandle handle {0};
    };

    struct Registry {
        std::mutex lock;
        std::unordered_map<RE::FormID, std::vector<BindingRecord>> byEffectSource;
        std::unordered_set<std::uint64_t> failedAdoptions;
    };

    thread_local std::vector<EventMirrorContext> activeEventMirrors;
    std::atomic<std::uint64_t> nextPendingMirrorID {1};
    std::mutex pendingEventMirrorsLock;
    std::vector<std::pair<std::uint64_t, EventMirrorContext>> pendingEventMirrors;

    [[nodiscard]] Registry& GetRegistry() {
        static Registry registry;
        return registry;
    }

    [[nodiscard]] bool ContainsScriptName(
        const std::vector<RE::BSFixedString>& a_scriptNames,
        const RE::BSFixedString& a_scriptName
    ) {
        return std::ranges::find(a_scriptNames, a_scriptName) != a_scriptNames.end();
    }

    [[nodiscard]] bool ContainsScriptName(
        const std::vector<std::string>& a_scriptNames,
        const RE::BSFixedString& a_scriptName
    ) {
        return std::ranges::any_of(a_scriptNames, [&](const auto& a_existing) {
            return a_existing == a_scriptName.c_str();
        });
    }

    [[nodiscard]] std::vector<std::string> ToStoredScriptNames(const std::vector<RE::BSFixedString>& a_scriptNames) {
        std::vector<std::string> storedNames;
        storedNames.reserve(a_scriptNames.size());
        for (const auto& scriptName : a_scriptNames) {
            storedNames.emplace_back(scriptName.c_str());
        }
        return storedNames;
    }

    [[nodiscard]] std::uint64_t MakeFailureKey(const RE::FormID a_sourceFormID, const RE::FormID a_effectSourceFormID) {
        return (static_cast<std::uint64_t>(a_sourceFormID) << 32) | a_effectSourceFormID;
    }

    [[nodiscard]] bool IsMirroredEventName(const RE::BSFixedString& a_eventName) {
        return std::strcmp(a_eventName.c_str(), kOnEquipped)
               == 0
               || std::strcmp(a_eventName.c_str(), kOnUnequipped)
               == 0;
    }

    [[nodiscard]] RE::VMHandle GetObjectHandle(const RE::TESForm& a_form) {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        auto* handlePolicy = vm ? vm->GetObjectHandlePolicy() : nullptr;
        if (!handlePolicy) {
            return 0;
        }

        return handlePolicy->GetHandleForObject(a_form.GetFormType(), std::addressof(a_form));
    }

    [[nodiscard]] RE::VMHandle GetObjectHandleByFormID(const RE::FormID a_formID) {
        auto* form = RE::TESForm::LookupByID(a_formID);
        return form ? GetObjectHandle(*form) : 0;
    }

    [[nodiscard]] RE::VMHandle GetParentHandle(const RE::VMHandle a_handle) {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        auto* handlePolicy = vm ? vm->GetObjectHandlePolicy() : nullptr;
        return handlePolicy && a_handle != 0 ? handlePolicy->GetParentHandle(a_handle) : 0;
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
        const bool a_loadedFromSave,
        const bool a_adoptedFromSave
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
                    .adoptedFromSave = a_adoptedFromSave,
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
        it->loadedFromSave = it->loadedFromSave || a_loadedFromSave;
        it->adoptedFromSave = it->adoptedFromSave || a_adoptedFromSave;
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
        bindingIt->adoptedFromSave = true;
        MergeOwnedScriptObjects(*bindingIt, a_ownedScriptObjects);
    }

    void MarkLoadedAdoptionFailure(const RE::FormID a_sourceFormID, const RE::FormID a_effectSourceFormID) {
        auto& registry = GetRegistry();
        std::scoped_lock lock(registry.lock);
        registry.failedAdoptions.insert(MakeFailureKey(a_sourceFormID, a_effectSourceFormID));
    }

    [[nodiscard]] bool ConsumeLoadedAdoptionFailure(
        const RE::FormID a_sourceFormID,
        const RE::FormID a_effectSourceFormID
    ) {
        auto& registry = GetRegistry();
        std::scoped_lock lock(registry.lock);
        return registry.failedAdoptions.erase(MakeFailureKey(a_sourceFormID, a_effectSourceFormID)) > 0;
    }

    void RemovePendingEventMirror(
        const RE::FormID a_sourceFormID,
        const RE::FormID a_effectSourceFormID,
        const RE::BSFixedString& a_eventName
    ) {
        std::scoped_lock lock(pendingEventMirrorsLock);
        std::erase_if(pendingEventMirrors, [&](const auto& a_entry) {
            const auto& context = a_entry.second;
            return context.sourceFormID
                   == a_sourceFormID
                   && context.effectSourceFormID
                   == a_effectSourceFormID
                   && context.eventName
                   == a_eventName;
        });
    }

    [[nodiscard]] std::optional<EventMirrorContext> ConsumePendingEventMirror(
        const RE::BSFixedString& a_eventName,
        const RE::VMHandle a_targetHandle,
        const std::optional<RE::FormID> a_actorFormID
    ) {
        std::scoped_lock lock(pendingEventMirrorsLock);

        auto match = pendingEventMirrors.end();
        for (auto it = pendingEventMirrors.begin(); it != pendingEventMirrors.end(); ++it) {
            const auto& context = it->second;
            const auto expectedEffectSourceHandle = GetObjectHandleByFormID(context.effectSourceFormID);
            const auto targetParentHandle = GetParentHandle(a_targetHandle);
            const auto eventMatches = context.eventName == a_eventName;
            const auto parentMatches = expectedEffectSourceHandle
                                       != 0
                                       && targetParentHandle
                                       == expectedEffectSourceHandle;
            const auto actorMatches = !context.expectedActorFormID
                                      || (a_actorFormID && *context.expectedActorFormID == *a_actorFormID);
            if (match == pendingEventMirrors.end() && eventMatches && actorMatches && parentMatches) {
                match = it;
            }
        }

        if (match == pendingEventMirrors.end()) {
            return std::nullopt;
        }

        auto context = std::move(match->second);
        pendingEventMirrors.erase(match);
        return context;
    }

    void ExpirePendingEventMirror(const std::uint64_t a_id) {
        std::scoped_lock lock(pendingEventMirrorsLock);
        const auto it = std::ranges::find_if(pendingEventMirrors, [a_id](const auto& a_entry) {
            return a_entry.first == a_id;
        });
        if (it == pendingEventMirrors.end()) {
            return;
        }

        pendingEventMirrors.erase(it);
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

    [[nodiscard]] bool ValidateExpectedScripts(
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

        auto cachedOwnedObjects = std::uint32_t {0};
        for (const auto& scriptName : a_binding.ownedScriptNames) {
            if (FindOwnedScriptObject(a_binding, scriptName)) {
                ++cachedOwnedObjects;
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

    [[nodiscard]] bool MirrorScriptsToEventHandle(
        const RE::TESForm& a_source,
        const RE::FormID a_effectSourceFormID,
        const RE::VMHandle a_targetHandle
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

        if (a_targetHandle == handlePolicy->EmptyHandle()) {
            return false;
        }

        const auto sourceHandle = handlePolicy->GetHandleForObject(a_source.GetFormType(), std::addressof(a_source));
        if (sourceHandle == handlePolicy->EmptyHandle()) {
            return false;
        }

        const auto vmadScriptNames = GetPrimaryVmadScriptNames(a_source);
        if (vmadScriptNames.empty()) {
            return false;
        }

        const auto existingScripts = GetAttachedScriptNames(*vm, a_targetHandle);
        std::vector<RE::BSFixedString> ownedScriptNames;
        std::vector<OwnedScriptObject> ownedScriptObjects;
        auto mirroredCount = std::uint32_t {0};
        for (const auto& scriptName : vmadScriptNames) {
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

            bindPolicy->BindObject(scriptObject, a_targetHandle);
            ownedScriptNames.push_back(scriptName);
            ownedScriptObjects.push_back(
                OwnedScriptObject {
                    .scriptName = scriptName.c_str(),
                    .object = scriptObject,
                }
            );
            ++mirroredCount;
        }

        if (mirroredCount > 0 || HasBindingForHandle(a_effectSourceFormID, a_targetHandle)) {
            RecordBinding(
                a_source.GetFormID(),
                a_effectSourceFormID,
                a_targetHandle,
                vmadScriptNames,
                ownedScriptNames,
                ownedScriptObjects,
                false,
                false
            );
        }

        return mirroredCount > 0 || HasBindingForHandle(a_effectSourceFormID, a_targetHandle);
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

    [[nodiscard]] bool AdoptLoadedBindingToEventHandle(
        const RE::TESForm& a_source,
        const RE::FormID a_effectSourceFormID,
        const RE::VMHandle a_targetHandle
    ) {
        auto binding = FindBindingRecord(a_source.GetFormID(), a_effectSourceFormID, true);
        if (!binding) {
            MarkLoadedAdoptionFailure(a_source.GetFormID(), a_effectSourceFormID);
            return false;
        }

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            MarkLoadedAdoptionFailure(a_source.GetFormID(), a_effectSourceFormID);
            return false;
        }

        auto* handlePolicy = vm->GetObjectHandlePolicy();
        if (!handlePolicy || a_targetHandle == handlePolicy->EmptyHandle()) {
            MarkLoadedAdoptionFailure(a_source.GetFormID(), a_effectSourceFormID);
            return false;
        }

        const auto savedHandle = binding->handle;
        if (savedHandle != a_targetHandle) {
            vm->MoveBoundObjects(savedHandle, a_targetHandle);
            binding->handle = a_targetHandle;
        }

        if (!ValidateExpectedScripts(*vm, *binding, a_targetHandle)) {
            static_cast<void>(UnbindOwnedScripts(*vm, *binding));
            MarkLoadedAdoptionFailure(a_source.GetFormID(), a_effectSourceFormID);
            return false;
        }

        MarkBindingAdopted(
            BindingHandle {
                .sourceFormID = a_source.GetFormID(),
                .effectSourceFormID = a_effectSourceFormID,
                .handle = a_targetHandle,
            },
            binding->ownedScriptObjects
        );
        return true;
    }

    void RemoveForEffectSourceImpl(const RE::FormID a_effectSourceFormID, RE::Actor* a_unequippedActor) {
        std::vector<BindingRecord> bindings;
        {
            auto& registry = GetRegistry();
            std::scoped_lock lock(registry.lock);
            const auto it = registry.byEffectSource.find(a_effectSourceFormID);
            if (it == registry.byEffectSource.end()) {
                return;
            }

            bindings = it->second;
        }

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (vm) {
            for (auto& binding : bindings) {
                static_cast<void>(ValidateExpectedScripts(*vm, binding, binding.handle));
                if (a_unequippedActor) {
                    static_cast<void>(
                        DispatchOwnedEventScripts(*vm, binding, *a_unequippedActor, RE::BSFixedString {kOnUnequipped})
                    );
                }
                static_cast<void>(UnbindOwnedScripts(*vm, binding));
            }
        }

        {
            auto& registry = GetRegistry();
            std::scoped_lock lock(registry.lock);
            registry.byEffectSource.erase(a_effectSourceFormID);
        }
    }

    void RemoveForSourceImpl(const RE::FormID a_sourceFormID, RE::Actor* a_unequippedActor) {
        std::vector<RE::FormID> effectSourcesToRemove;
        {
            auto& registry = GetRegistry();
            std::scoped_lock lock(registry.lock);
            for (const auto& [effectSourceFormID, bindings] : registry.byEffectSource) {
                const auto hasSourceBinding = std::ranges::any_of(bindings, [a_sourceFormID](const auto& a_binding) {
                    return a_binding.sourceFormID == a_sourceFormID;
                });
                if (hasSourceBinding) {
                    effectSourcesToRemove.push_back(effectSourceFormID);
                }
            }
        }

        for (const auto effectSourceFormID : effectSourcesToRemove) {
            RemoveForEffectSourceImpl(effectSourceFormID, a_unequippedActor);
        }
    }

    void BeginScopedMirrorImpl(
        const RE::TESForm& a_source,
        const RE::TESForm& a_effectSource,
        const bool a_adoptLoadedBinding
    ) {
        activeEventMirrors.push_back(
            EventMirrorContext {
                .sourceFormID = a_source.GetFormID(),
                .effectSourceFormID = a_effectSource.GetFormID(),
                .eventName = {},
                .adoptLoadedBinding = a_adoptLoadedBinding,
            }
        );
    }

    void EndScopedMirrorImpl() {
        if (activeEventMirrors.empty()) {
            return;
        }

        activeEventMirrors.pop_back();
    }

    bool BeginPendingMirrorImpl(
        const RE::TESForm& a_source,
        const RE::TESForm& a_effectSource,
        const RE::BSFixedString& a_eventName,
        const bool a_adoptLoadedBinding,
        const std::optional<RE::FormID> a_expectedActorFormID
    ) {
        if (!IsMirroredEventName(a_eventName)) {
            return false;
        }

        if (!a_adoptLoadedBinding) {
            const auto scriptNames = GetPrimaryVmadScriptNames(a_source);
            if (scriptNames.empty()) {
                return false;
            }
        }

        const auto id = nextPendingMirrorID.fetch_add(1);
        {
            std::scoped_lock lock(pendingEventMirrorsLock);
            pendingEventMirrors.emplace_back(
                id,
                EventMirrorContext {
                    .sourceFormID = a_source.GetFormID(),
                    .effectSourceFormID = a_effectSource.GetFormID(),
                    .eventName = a_eventName,
                    .expectedActorFormID = a_expectedActorFormID,
                    .adoptLoadedBinding = a_adoptLoadedBinding,
                }
            );
        }

        stl::add_thread_task(
            [id] {
                ExpirePendingEventMirror(id);
            },
            kPendingMirrorLifetime
        );

        return true;
    }

    bool MirrorActiveTargetImpl(
        const RE::VMHandle a_handle,
        const RE::BSFixedString& a_eventName,
        const std::optional<RE::FormID> a_actorFormID
    ) {
        if (!IsMirroredEventName(a_eventName)) {
            return false;
        }

        std::optional<EventMirrorContext> pendingContext;
        const auto* context = activeEventMirrors.empty() ? nullptr : std::addressof(activeEventMirrors.back());
        if (!context) {
            pendingContext = ConsumePendingEventMirror(a_eventName, a_handle, a_actorFormID);
            if (!pendingContext) {
                return false;
            }
            context = std::addressof(*pendingContext);
        }

        const auto* source = RE::TESForm::LookupByID(context->sourceFormID);
        if (!source) {
            if (context->adoptLoadedBinding) {
                MarkLoadedAdoptionFailure(context->sourceFormID, context->effectSourceFormID);
            }
            return false;
        }

        const auto handled = context->adoptLoadedBinding
                                 ? AdoptLoadedBindingToEventHandle(*source, context->effectSourceFormID, a_handle)
                                 : MirrorScriptsToEventHandle(*source, context->effectSourceFormID, a_handle);
        if (!activeEventMirrors.empty()) {
            RemovePendingEventMirror(context->sourceFormID, context->effectSourceFormID, a_eventName);
        }
        return handled;
    }

    bool MirrorScriptsAndDispatchImpl(
        const RE::TESForm& a_source,
        const RE::FormID a_effectSourceFormID,
        const RE::VMHandle a_targetHandle,
        RE::Actor& a_actor,
        const RE::BSFixedString& a_eventName
    ) {
        if (!IsMirroredEventName(a_eventName)) {
            return false;
        }

        const auto mirrored = MirrorScriptsToEventHandle(a_source, a_effectSourceFormID, a_targetHandle);
        if (!mirrored) {
            return false;
        }

        RemovePendingEventMirror(a_source.GetFormID(), a_effectSourceFormID, a_eventName);
        return DispatchMirroredScriptsForEvent(a_source, a_effectSourceFormID, a_actor, a_eventName);
    }

    void RemoveEffectSourceBindings(const RE::FormID a_effectSourceFormID) {
        RemoveForEffectSourceImpl(a_effectSourceFormID, nullptr);
    }

    void RemoveSourceBindings(const RE::FormID a_sourceFormID) {
        RemoveForSourceImpl(a_sourceFormID, nullptr);
    }

    void RemoveSourceBindingsForUnequip(const RE::FormID a_sourceFormID, RE::Actor& a_unequippedActor) {
        RemoveForSourceImpl(a_sourceFormID, std::addressof(a_unequippedActor));
    }

    bool ValidateLoadedRestoreImpl(const RE::TESForm& a_source, const RE::FormID a_effectSourceFormID) {
        const auto expectedScriptNames = GetPrimaryVmadScriptNames(a_source);
        if (expectedScriptNames.empty()) {
            return true;
        }

        const auto binding = FindBindingRecord(a_source.GetFormID(), a_effectSourceFormID, true);
        if (!binding) {
            return false;
        }

        return std::ranges::all_of(expectedScriptNames, [&](const auto& scriptName) {
            return ContainsScriptName(binding->expectedScriptNames, scriptName);
        });
    }

    bool AdoptLoadedBindingImpl(const RE::TESForm& a_source, const RE::FormID a_effectSourceFormID) {
        auto binding = FindBindingRecord(a_source.GetFormID(), a_effectSourceFormID, true);
        if (!binding) {
            return false;
        }

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            return false;
        }

        if (!ValidateExpectedScripts(*vm, *binding, binding->handle)) {
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

    bool HasLoadedBindingImpl(const RE::FormID a_sourceFormID, const RE::FormID a_effectSourceFormID) {
        return FindBindingRecord(a_sourceFormID, a_effectSourceFormID, true).has_value();
    }

    std::optional<RE::VMHandle> GetHandleImpl(const RE::FormID a_sourceFormID, const RE::FormID a_effectSourceFormID) {
        const auto binding = FindBindingRecord(a_sourceFormID, a_effectSourceFormID, false);
        if (!binding) {
            return std::nullopt;
        }

        return binding->handle;
    }

    bool ConsumeLoadedFailureImpl(const RE::FormID a_sourceFormID, const RE::FormID a_effectSourceFormID) {
        return ConsumeLoadedAdoptionFailure(a_sourceFormID, a_effectSourceFormID);
    }

    [[nodiscard]] bool WriteString(SKSE::SerializationInterface& a_intfc, const std::string& a_value) {
        const auto length = static_cast<std::uint32_t>(a_value.size());
        if (!a_intfc.WriteRecordData(length)) {
            return false;
        }

        if (length == 0) {
            return true;
        }

        return a_intfc.WriteRecordData(a_value.data(), length);
    }

    [[nodiscard]] std::optional<std::string> ReadString(SKSE::SerializationInterface& a_intfc) {
        std::uint32_t length = 0;
        if (a_intfc.ReadRecordData(length) != sizeof(length)) {
            return std::nullopt;
        }

        std::string value(length, '\0');
        if (length == 0) {
            return value;
        }

        if (a_intfc.ReadRecordData(value.data(), length) != length) {
            return std::nullopt;
        }

        return value;
    }

    void SaveBindings(
        SKSE::SerializationInterface& a_intfc,
        const std::vector<ScriptBindingSource>& a_selectedSources
    ) {
        if (a_selectedSources.empty()) {
            return;
        }

        std::vector<BindingRecord> bindings;
        {
            auto& registry = GetRegistry();
            std::scoped_lock lock(registry.lock);
            for (const auto& effectSourceBindings : registry.byEffectSource | std::views::values) {
                for (const auto& binding : effectSourceBindings) {
                    const auto selected = std::ranges::any_of(a_selectedSources, [&](const auto& a_selectedSource) {
                        return a_selectedSource.sourceFormID
                               == binding.sourceFormID
                               && a_selectedSource.effectSourceFormID
                               == binding.effectSourceFormID;
                    });
                    if (selected) {
                        bindings.push_back(binding);
                    }
                }
            }
        }

        if (bindings.empty()) {
            return;
        }

        if (!a_intfc.OpenRecord(kRecordPapyrusBindings, kPapyrusBindingsVersion)) {
            logger::error("Papyrus: save failed | reason=openRecord");
            return;
        }

        const auto bindingCount = static_cast<std::uint32_t>(bindings.size());
        if (!a_intfc.WriteRecordData(bindingCount)) {
            logger::error("Papyrus: save failed | reason=writeCount");
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

            if (!a_intfc.WriteRecordData(header)) {
                logger::error(
                    "Papyrus: save failed | source={:08X} | effectSource={:08X} | handle={:016X} | reason=writeHeader",
                    binding.sourceFormID,
                    binding.effectSourceFormID,
                    binding.handle
                );
                return;
            }

            for (const auto& scriptName : binding.expectedScriptNames) {
                if (!WriteString(a_intfc, scriptName)) {
                    logger::error(
                        "Papyrus: save failed | source={:08X} | effectSource={:08X} | handle={:016X} | script={} | reason=writeExpectedScript",
                        binding.sourceFormID,
                        binding.effectSourceFormID,
                        binding.handle,
                        scriptName
                    );
                    return;
                }
            }

            for (const auto& scriptName : binding.ownedScriptNames) {
                if (!WriteString(a_intfc, scriptName)) {
                    logger::error(
                        "Papyrus: save failed | source={:08X} | effectSource={:08X} | handle={:016X} | script={} | reason=writeOwnedScript",
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

    bool LoadBindingRecord(const Serialization::RecordInfo a_recordInfo, SKSE::SerializationInterface& a_intfc) {
        if (a_recordInfo.type != kRecordPapyrusBindings) {
            return false;
        }

        if (a_recordInfo.version != kPapyrusBindingsVersion) {
            logger::warn("Papyrus: load skipped | version={} | reason=unsupportedVersion", a_recordInfo.version);
            return true;
        }

        std::uint32_t bindingCount = 0;
        if (a_intfc.ReadRecordData(bindingCount) != sizeof(bindingCount)) {
            logger::error("Papyrus: load failed | reason=readCount");
            return true;
        }

        auto loadedCount = std::uint32_t {0};
        auto skippedCount = std::uint32_t {0};
        for (std::uint32_t index = 0; index < bindingCount; ++index) {
            StoredBindingHeader stored;
            if (a_intfc.ReadRecordData(stored) != sizeof(stored)) {
                logger::error("Papyrus: load failed | index={} | reason=readHeader", index);
                return true;
            }

            std::vector<RE::BSFixedString> expectedScriptNames;
            expectedScriptNames.reserve(stored.expectedScriptCount);
            for (std::uint32_t scriptIndex = 0; scriptIndex < stored.expectedScriptCount; ++scriptIndex) {
                const auto scriptName = ReadString(a_intfc);
                if (!scriptName) {
                    logger::error(
                        "Papyrus: load failed | index={} | scriptIndex={} | reason=readExpectedScript",
                        index,
                        scriptIndex
                    );
                    return true;
                }
                expectedScriptNames.emplace_back(*scriptName);
            }

            std::vector<RE::BSFixedString> ownedScriptNames;
            ownedScriptNames.reserve(stored.ownedScriptCount);
            for (std::uint32_t scriptIndex = 0; scriptIndex < stored.ownedScriptCount; ++scriptIndex) {
                const auto scriptName = ReadString(a_intfc);
                if (!scriptName) {
                    logger::error(
                        "Papyrus: load failed | index={} | scriptIndex={} | reason=readOwnedScript",
                        index,
                        scriptIndex
                    );
                    return true;
                }
                ownedScriptNames.emplace_back(*scriptName);
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
                ++skippedCount;
                logger::warn(
                    "Papyrus: entry skipped | index={} | source={:08X} | effectSource={:08X} | handle={:016X} | sourceResolved={} | effectSourceResolved={} | handleResolved={} | reason=resolveFailed",
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
                true,
                false
            );
            ++loadedCount;
        }

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

        {
            std::scoped_lock pendingLock(pendingEventMirrorsLock);
            pendingEventMirrors.clear();
        }

        {
            auto& registry = GetRegistry();
            std::scoped_lock lock(registry.lock);
            registry.failedAdoptions.clear();
        }
    }
}

ScopedMirror::ScopedMirror(
    const RE::TESForm& a_source,
    const RE::TESForm& a_effectSource,
    const bool a_adoptLoadedBinding
) {
    BeginScopedMirrorImpl(a_source, a_effectSource, a_adoptLoadedBinding);
    active_ = true;
}

ScopedMirror::~ScopedMirror() {
    if (!active_) {
        return;
    }

    EndScopedMirrorImpl();
}

bool BeginPendingMirror(
    const RE::TESForm& a_source,
    const RE::TESForm& a_effectSource,
    const RE::BSFixedString& a_eventName,
    const bool a_adoptLoadedBinding,
    const std::optional<RE::FormID> a_expectedActorFormID
) {
    return BeginPendingMirrorImpl(a_source, a_effectSource, a_eventName, a_adoptLoadedBinding, a_expectedActorFormID);
}

bool MirrorActiveTarget(
    const RE::VMHandle a_handle,
    const RE::BSFixedString& a_eventName,
    const std::optional<RE::FormID> a_actorFormID
) {
    return MirrorActiveTargetImpl(a_handle, a_eventName, a_actorFormID);
}

bool MirrorScriptsAndDispatch(
    const RE::TESForm& a_source,
    const RE::FormID a_effectSourceFormID,
    const RE::VMHandle a_targetHandle,
    RE::Actor& a_actor,
    const RE::BSFixedString& a_eventName
) {
    return MirrorScriptsAndDispatchImpl(a_source, a_effectSourceFormID, a_targetHandle, a_actor, a_eventName);
}

void RemoveForEffectSource(const RE::FormID a_effectSourceFormID) {
    RemoveEffectSourceBindings(a_effectSourceFormID);
}

void RemoveForEffectSource(const RE::FormID a_effectSourceFormID, RE::Actor& a_unequippedActor) {
    RemoveForEffectSourceImpl(a_effectSourceFormID, std::addressof(a_unequippedActor));
}

void RemoveForSource(const RE::FormID a_sourceFormID) {
    RemoveSourceBindings(a_sourceFormID);
}

void RemoveForSource(const RE::FormID a_sourceFormID, RE::Actor& a_unequippedActor) {
    RemoveSourceBindingsForUnequip(a_sourceFormID, a_unequippedActor);
}

bool ValidateLoadedRestore(const RE::TESForm& a_source, const RE::FormID a_effectSourceFormID) {
    return ValidateLoadedRestoreImpl(a_source, a_effectSourceFormID);
}

bool AdoptLoadedBinding(const RE::TESForm& a_source, const RE::FormID a_effectSourceFormID) {
    return AdoptLoadedBindingImpl(a_source, a_effectSourceFormID);
}

bool HasLoadedBinding(const RE::FormID a_sourceFormID, const RE::FormID a_effectSourceFormID) {
    return HasLoadedBindingImpl(a_sourceFormID, a_effectSourceFormID);
}

std::optional<RE::VMHandle> GetHandle(const RE::FormID a_sourceFormID, const RE::FormID a_effectSourceFormID) {
    return GetHandleImpl(a_sourceFormID, a_effectSourceFormID);
}

bool ConsumeLoadedFailure(const RE::FormID a_sourceFormID, const RE::FormID a_effectSourceFormID) {
    return ConsumeLoadedFailureImpl(a_sourceFormID, a_effectSourceFormID);
}

void Save(SKSE::SerializationInterface& a_intfc, const std::vector<ScriptBindingSource>& a_selectedSources) {
    SaveBindings(a_intfc, a_selectedSources);
}

bool LoadRecord(const Serialization::RecordInfo a_recordInfo, SKSE::SerializationInterface& a_intfc) {
    return LoadBindingRecord(a_recordInfo, a_intfc);
}

void Revert() {
    RevertBindings();
}
}
