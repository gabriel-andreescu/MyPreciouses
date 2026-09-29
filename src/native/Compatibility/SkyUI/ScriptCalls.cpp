#include "Compatibility/SkyUI/ScriptCalls.h"

#include <RE/Skyrim.h> // IWYU pragma: keep
#include <SKSE/SKSE.h> // IWYU pragma: keep

#include "Compatibility/SkyUI/FavoritesManager.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace Compatibility::SkyUI::ScriptCalls {
namespace {
    using Function = RE::BSScript::Internal::ScriptFunction;
    using Result = RE::BSScript::IFunction::CallResult;
    using StackPtr = RE::BSTSmartPointer<RE::BSScript::Stack>;

    struct Pending {
        std::atomic<bool> ready {false};
        FavoritesManager::Outcome outcome;
    };

    std::mutex g_lock;
    std::atomic<std::uint64_t> g_generation {0};

    [[nodiscard]] auto& PendingCalls() {
        static std::unordered_map<RE::VMStackID, std::shared_ptr<Pending>> pending;
        return pending;
    }

    void Begin(const RE::VMStackID a_stack, FavoritesManager::Call a_call) {
        const auto pending = std::make_shared<Pending>();
        PendingCalls().emplace(a_stack, pending);
        const auto generation = g_generation.load();
        // ScriptFunction::Call can run on VM workers. Retry keeps the call in the VM until the game task completes.
        SKSE::GetTaskInterface()->AddTask([pending, generation, call = std::move(a_call)] {
            if (generation == g_generation.load()) {
                pending->outcome = FavoritesManager::Run(call);
            }
            pending->ready.store(true, std::memory_order_release);
        });
    }

    struct ScriptCall {
        static Result thunk(
            Function* a_function,
            const StackPtr& a_stack,
            RE::BSScript::ErrorLogger* a_logger,
            RE::BSScript::Internal::VirtualMachine* a_vm,
            bool a_tasklet
        ) {
            if (!FavoritesManager::IsManager(*a_function)) {
                return func(a_function, a_stack, a_logger, a_vm, a_tasklet);
            }
            {
                std::scoped_lock const lock(g_lock);
                auto& pendingCalls = PendingCalls();
                const auto found = pendingCalls.find(a_stack->stackID);
                if (found == pendingCalls.end()) {
                    if (auto call = FavoritesManager::ReadCall(*a_function, *a_stack->top)) {
                        Begin(a_stack->stackID, std::move(*call));
                        return Result::kFailedRetry;
                    }
                } else {
                    const auto pending = found->second;
                    if (!pending->ready.load(std::memory_order_acquire)) {
                        return Result::kFailedRetry;
                    }
                    pendingCalls.erase(found);
                    if (pending->outcome.handled) {
                        FavoritesManager::Complete(*a_stack, pending->outcome);
                        return Result::kCompleted;
                    }
                }
            }
            return func(a_function, a_stack, a_logger, a_vm, a_tasklet);
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };
}

void Install() {
    REL::Relocation<std::uintptr_t> table {Function::VTABLE[0]};
    ScriptCall::func = table.write_vfunc(0x0F, ScriptCall::thunk);
}

void Revert() {
    std::scoped_lock const lock(g_lock);
    ++g_generation;
    PendingCalls().clear();
}
}
