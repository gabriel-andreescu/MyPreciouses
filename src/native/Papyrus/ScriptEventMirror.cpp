#include "Papyrus/ScriptEventMirror.h"

#include <RE/Skyrim.h> // IWYU pragma: keep
#include <REL/Relocation.h>
#include <SKSE/SKSE.h> // IWYU pragma: keep

#include "Core/ActorKey.h"
#include "Core/ItemSource.h"
#include "Inventory.h"
#include "Serialization.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Papyrus::ScriptEventMirror {
namespace {
    constexpr auto kRecordBindings = Serialization::MakeRecordType('P', 'Y', 'B', 'D');
    constexpr std::uint32_t kRecordVersion = 1;

    struct Binding {
        RE::FormID sourceFormID {0};
        RE::VMHandle handle {0};
        bool suspended {false};
        bool loadedFromSave {false};
    };

    struct BindingKey {
        Core::ActorKey actor;
        RE::FormID effectSourceFormID {0};

        [[nodiscard]] bool operator==(const BindingKey&) const = default;
    };

    struct BindingKeyHash {
        [[nodiscard]] std::size_t operator()(const BindingKey& a_key) const noexcept {
            return std::hash<std::uint64_t> {}(
                (static_cast<std::uint64_t>(a_key.actor.referenceFormID) << 32U) | a_key.effectSourceFormID
            );
        }
    };

    std::mutex g_lock;
    [[nodiscard]] auto& Bindings() {
        static std::unordered_map<BindingKey, Binding, BindingKeyHash> bindings;
        return bindings;
    }

    [[nodiscard]] RE::VMHandle InventoryHandle(const std::uint16_t a_uniqueID, const RE::FormID a_container) {
        using Function = RE::VMHandle (*)(std::uint16_t, RE::FormID);
        static REL::Relocation<Function> const function {REL::VariantID(52664, 53517, 0x93DF10)};
        return function(a_uniqueID, a_container);
    }

    [[nodiscard]] bool HasScripts(RE::BSScript::Internal::VirtualMachine& a_vm, const RE::VMHandle a_handle) {
        RE::BSSpinLockGuard const lock {a_vm.attachedScriptsLock};
        const auto scripts = a_vm.attachedScripts.find(a_handle);
        return scripts != a_vm.attachedScripts.end() && !scripts->second.empty();
    }

    [[nodiscard]] RE::VMHandle ResolveScriptHandle(
        RE::BSScript::Internal::VirtualMachine& a_vm,
        const RE::Actor& a_actor,
        RE::ExtraDataList& a_extraList
    ) {
        RE::VMHandle handle = 0;
        if (const auto* uniqueID = a_extraList.GetByType<RE::ExtraUniqueID>(); uniqueID && uniqueID->uniqueID != 0) {
            handle = InventoryHandle(uniqueID->uniqueID, a_actor.GetFormID());
        }
        if (auto* referenceHandle = a_extraList.GetByType<RE::ExtraReferenceHandle>();
            referenceHandle && (handle == 0 || !HasScripts(a_vm, handle))) {
            if (const auto original = referenceHandle->GetOriginalReference()) {
                auto* policy = a_vm.GetObjectHandlePolicy();
                handle = policy->GetHandleForObject(original->GetFormType(), original.get());
            }
        }
        return handle;
    }

    [[nodiscard]] std::optional<RE::VMHandle> FindInventoryScriptHandle(
        RE::Actor& a_actor,
        const RE::TESObjectARMO& a_ring,
        const Core::ItemSource& a_source
    ) {
        auto* virtualMachine = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        auto const* entry = Inventory::FindEntry(a_actor, a_ring);
        if (!virtualMachine || !entry || !entry->extraLists) {
            return std::nullopt;
        }
        for (auto* extraList : *entry->extraLists) {
            if (!extraList || Inventory::HasRightWornFlag(extraList)) {
                continue;
            }
            if (!Inventory::MatchesSource(extraList, a_source)) {
                continue;
            }
            const auto handle = ResolveScriptHandle(*virtualMachine, a_actor, *extraList);
            if (handle == 0) {
                continue;
            }
            const auto claimed = [&] {
                std::scoped_lock const lock(g_lock);
                return std::ranges::any_of(Bindings() | std::views::values, [handle](const Binding& a_binding) {
                    return a_binding.handle == handle;
                });
            }();
            if (!claimed && HasScripts(*virtualMachine, handle)) {
                return handle;
            }
        }
        return std::nullopt;
    }

    bool SendEvent(const Binding& a_binding, RE::Actor& a_actor, const char* a_event) {
        auto* virtualMachine = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!virtualMachine) {
            return false;
        }
        // The VM owns inventory scripts and serializes their state with the item.
        auto* args = RE::MakeFunctionArguments(std::addressof(a_actor));
        virtualMachine->SendEvent(a_binding.handle, RE::BSFixedString {a_event}, args);
        delete args;
        return true;
    }

    [[nodiscard]] std::optional<Binding> TakeBinding(const BindingKey a_key) {
        std::scoped_lock const lock(g_lock);
        auto const node = Bindings().extract(a_key);
        return node.empty() ? std::nullopt : std::optional {node.mapped()};
    }
}

std::vector<BindingSnapshot> GetBindingSnapshots() {
    std::vector<BindingSnapshot> snapshots;
    std::scoped_lock const lock(g_lock);
    snapshots.reserve(Bindings().size());
    for (const auto& [key, binding] : Bindings()) {
        snapshots.push_back({
            .actor = key.actor,
            .sourceFormID = binding.sourceFormID,
            .effectSourceFormID = key.effectSourceFormID,
            .suspended = binding.suspended,
        });
    }
    return snapshots;
}

std::optional<Core::ExtraUniqueIDKey> FindBoundCopyIdentity(
    RE::Actor& a_actor,
    const Core::ItemSource& a_source,
    const RE::FormID a_effectSourceFormID
) {
    RE::VMHandle handle = 0;
    {
        std::scoped_lock const lock(g_lock);
        const auto binding = Bindings().find(
            {.actor = Core::MakeActorKey(a_actor), .effectSourceFormID = a_effectSourceFormID}
        );
        if (binding == Bindings().end() || binding->second.sourceFormID != a_source.sourceFormID) {
            return std::nullopt;
        }
        handle = binding->second.handle;
    }
    auto* virtualMachine = RE::BSScript::Internal::VirtualMachine::GetSingleton();
    auto const* ring = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_source.sourceFormID);
    auto const* entry = ring ? Inventory::FindEntry(a_actor, *ring) : nullptr;
    if (!virtualMachine || !entry || !entry->extraLists) {
        return std::nullopt;
    }
    for (auto* extraList : *entry->extraLists) {
        if (extraList && ResolveScriptHandle(*virtualMachine, a_actor, *extraList) == handle) {
            return Inventory::EnsureExtraUniqueIDKey(a_actor, *ring, *extraList);
        }
    }
    return std::nullopt;
}

void HandleUniqueIDChange(const RE::TESUniqueIDChangeEvent& a_event) {
    if (a_event.oldUniqueID == 0) {
        return;
    }
    const auto previous = InventoryHandle(a_event.oldUniqueID, a_event.oldBaseID);
    std::vector<Binding> unequipped;
    {
        std::scoped_lock const lock(g_lock);
        for (auto& binding : Bindings() | std::views::values) {
            if (binding.handle != previous) {
                continue;
            }
            if (a_event.oldBaseID != a_event.newBaseID && !binding.suspended) {
                unequipped.push_back(binding);
                binding.suspended = true;
            }
            if (a_event.newUniqueID != 0) {
                binding.handle = InventoryHandle(a_event.newUniqueID, a_event.newBaseID);
            }
        }
    }
    if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_event.oldBaseID)) {
        for (const auto& binding : unequipped) {
            // Dispatch before SkyrimVM moves the item's scripts to their new handle.
            static_cast<void>(SendEvent(binding, *actor, "OnUnequipped"));
        }
    }
}

bool DispatchEquipped(
    RE::Actor& a_actor,
    const RE::TESObjectARMO& a_ring,
    const Core::ItemSource& a_source,
    RE::TESObjectARMO const& a_effectSource
) {
    const BindingKey key {.actor = Core::MakeActorKey(a_actor), .effectSourceFormID = a_effectSource.GetFormID()};
    std::optional<Binding> resumed;
    {
        std::scoped_lock const lock(g_lock);
        const auto binding = Bindings().find(key);
        if (binding != Bindings().end()) {
            if (!binding->second.suspended) {
                binding->second.loadedFromSave = false;
                return true;
            }
            binding->second.suspended = false;
            binding->second.loadedFromSave = false;
            resumed = binding->second;
        }
    }
    if (resumed) {
        return SendEvent(*resumed, a_actor, "OnEquipped");
    }
    const auto handle = FindInventoryScriptHandle(a_actor, a_ring, a_source);
    if (!handle) {
        return false;
    }
    const Binding binding {.sourceFormID = a_ring.GetFormID(), .handle = *handle};
    {
        std::scoped_lock const lock(g_lock);
        Bindings().insert_or_assign(key, binding);
    }
    return SendEvent(binding, a_actor, "OnEquipped");
}

void RemoveEffectSourceBindings(const Core::ActorKey a_actor, const RE::FormID a_effectSourceFormID) {
    static_cast<void>(TakeBinding({.actor = a_actor, .effectSourceFormID = a_effectSourceFormID}));
}

void RemoveEffectSourceBindingsForUnequip(const RE::FormID a_effectSourceFormID, RE::Actor& a_actor) {
    const auto binding = TakeBinding(
        {.actor = Core::MakeActorKey(a_actor), .effectSourceFormID = a_effectSourceFormID}
    );
    if (binding && !binding->suspended) {
        static_cast<void>(SendEvent(*binding, a_actor, "OnUnequipped"));
    }
}

void SuspendEffectSourceBindingsForUnequip(const RE::FormID a_effectSourceFormID, RE::Actor& a_actor) {
    std::optional<Binding> suspended;
    {
        std::scoped_lock const lock(g_lock);
        const auto binding = Bindings().find(
            {.actor = Core::MakeActorKey(a_actor), .effectSourceFormID = a_effectSourceFormID}
        );
        if (binding != Bindings().end() && !binding->second.suspended) {
            binding->second.suspended = true;
            suspended = binding->second;
        }
    }
    if (suspended) {
        static_cast<void>(SendEvent(*suspended, a_actor, "OnUnequipped"));
    }
}

bool HasLoadedActiveBinding(
    const Core::ActorKey a_actor,
    const RE::FormID a_sourceFormID,
    const RE::FormID a_effectSourceFormID
) {
    std::scoped_lock const lock(g_lock);
    const auto binding = Bindings().find({.actor = a_actor, .effectSourceFormID = a_effectSourceFormID});
    return binding
           != Bindings().end()
           && binding->second.sourceFormID
           == a_sourceFormID
           && binding->second.loadedFromSave
           && !binding->second.suspended;
}

void SaveBindings(SKSE::SerializationInterface& a_intfc, const std::vector<BindingRetentionKey>& a_retainedBindings) {
    std::vector<std::pair<BindingKey, Binding>> retained;
    {
        std::scoped_lock const lock(g_lock);
        for (const auto& key : a_retainedBindings) {
            const auto binding = Bindings().find({.actor = key.actor, .effectSourceFormID = key.effectSourceFormID});
            if (binding != Bindings().end() && binding->second.sourceFormID == key.sourceFormID) {
                retained.emplace_back(*binding);
            }
        }
    }
    if (retained.empty()) {
        return;
    }
    if (!a_intfc.OpenRecord(kRecordBindings, kRecordVersion)
        || !Serialization::WriteField(a_intfc, static_cast<std::uint32_t>(retained.size()))) {
        SKSE::log::error("Papyrus: cannot write inventory script bindings");
        return;
    }
    for (const auto& [key, binding] : retained) {
        if (!Serialization::WriteField(a_intfc, key.actor.referenceFormID)
            || !Serialization::WriteField(a_intfc, key.effectSourceFormID)
            || !Serialization::WriteField(a_intfc, binding.sourceFormID)
            || !Serialization::WriteField(a_intfc, binding.handle)
            || !Serialization::WriteField(a_intfc, static_cast<std::uint8_t>(binding.suspended))) {
            SKSE::log::error("Papyrus: cannot save script binding for ring {:08X}", binding.sourceFormID);
            return;
        }
    }
}

bool TryLoadBindingRecord(const Serialization::RecordInfo a_recordInfo, SKSE::SerializationInterface& a_intfc) {
    if (a_recordInfo.type != kRecordBindings) {
        return false;
    }
    auto remaining = a_recordInfo.length;
    const auto read = [&] {
        if (a_recordInfo.version != kRecordVersion) {
            SKSE::log::error("Papyrus: unsupported script binding record version {}", a_recordInfo.version);
            return;
        }
        std::uint32_t count = 0;
        constexpr auto kEntrySize = (sizeof(RE::FormID) * 3) + sizeof(RE::VMHandle) + sizeof(std::uint8_t);
        if (!Serialization::ReadField(a_intfc, remaining, count) || count > remaining / kEntrySize) {
            SKSE::log::error("Papyrus: truncated script binding record");
            return;
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            BindingKey key;
            Binding binding;
            std::uint8_t suspended = 0;
            if (!Serialization::ReadField(a_intfc, remaining, key.actor.referenceFormID)
                || !Serialization::ReadField(a_intfc, remaining, key.effectSourceFormID)
                || !Serialization::ReadField(a_intfc, remaining, binding.sourceFormID)
                || !Serialization::ReadField(a_intfc, remaining, binding.handle)
                || !Serialization::ReadField(a_intfc, remaining, suspended)) {
                SKSE::log::error("Papyrus: truncated script binding at index {}", i);
                return;
            }
            const auto source = binding.sourceFormID;
            if (!a_intfc.ResolveFormID(key.actor.referenceFormID, key.actor.referenceFormID)
                || !a_intfc.ResolveFormID(key.effectSourceFormID, key.effectSourceFormID)
                || !a_intfc.ResolveFormID(source, binding.sourceFormID)
                || !a_intfc.ResolveHandle(binding.handle, binding.handle)) {
                SKSE::log::warn("Papyrus: cannot resolve saved inventory script for ring {:08X}", source);
                continue;
            }
            binding.suspended = suspended != 0;
            binding.loadedFromSave = true;
            std::scoped_lock const lock(g_lock);
            Bindings().insert_or_assign(key, binding);
        }
    };
    read();
    Serialization::DrainRecordData(a_intfc, remaining);
    return true;
}

void RevertBindings() {
    std::scoped_lock const lock(g_lock);
    Bindings().clear();
}
}
