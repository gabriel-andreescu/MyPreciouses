#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOGDI

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>

#define DLLEXPORT __declspec(dllexport)

namespace logger = SKSE::log;

using namespace std::literals;

namespace stl {
using namespace SKSE::stl;

template <typename T, std::size_t Size = 5>
void write_thunk_call(REL::Relocation<> a_target) noexcept {
    T::func = a_target.write_call<Size>(T::thunk);
}

inline void add_task(const std::function<void()>& a_fn) {
    if (const auto* tasks = SKSE::GetTaskInterface()) {
        tasks->AddTask(a_fn);
    } else {
        a_fn();
    }
}

inline void add_ui_task(const std::function<void()>& a_fn) {
    if (const auto* tasks = SKSE::GetTaskInterface()) {
        tasks->AddUITask(a_fn);
    } else {
        a_fn();
    }
}

template <class Rep, class Period>
void add_thread_task(const std::function<void()>& a_fn, const std::chrono::duration<Rep, Period> a_wait_for) {
    std::jthread {[=] {
        std::this_thread::sleep_for(a_wait_for);
        add_task(a_fn);
    }}.detach();
}
}
