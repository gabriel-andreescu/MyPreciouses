#include "Visuals/FirstPersonRetry.h"

#include <SKSE/SKSE.h>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>

namespace Visuals::FirstPersonRetry {
namespace {
    class Timer {
    public:
        void Schedule(std::function<void()> a_refresh) {
            std::scoped_lock const lock(_mutex);
            if (!_refresh) {
                _deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            }
            _refresh = std::move(a_refresh);
            _changed.notify_one();
        }

        void Cancel() {
            std::scoped_lock const lock(_mutex);
            _refresh = {};
            _changed.notify_one();
        }

    private:
        void Run(const std::stop_token& a_stop) {
            std::unique_lock lock(_mutex);
            while (_changed.wait(lock, a_stop, [this] { return static_cast<bool>(_refresh); })) {
                const auto deadline = _deadline;
                if (_changed.wait_until(lock, a_stop, deadline, [this, deadline] {
                        return !_refresh || _deadline != deadline;
                    })) {
                    continue;
                }
                if (a_stop.stop_requested()) {
                    return;
                }
                auto refresh = std::exchange(_refresh, {});
                lock.unlock();
                SKSE::GetTaskInterface()->AddTask(std::move(refresh));
                lock.lock();
            }
        }

        std::mutex _mutex;
        std::condition_variable_any _changed;
        std::function<void()> _refresh;
        std::chrono::steady_clock::time_point _deadline;
        std::jthread _worker {[this](const std::stop_token& a_stop) { Run(a_stop); }};
    };

    Timer& GetTimer() {
        static Timer timer;
        return timer;
    }
}

void Schedule(std::function<void()> a_refresh) {
    GetTimer().Schedule(std::move(a_refresh));
}

void Cancel() {
    GetTimer().Cancel();
}
}
