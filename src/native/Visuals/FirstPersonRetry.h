#pragma once

#include <functional>

namespace Visuals::FirstPersonRetry {
void Schedule(std::function<void()> a_refresh);
void Cancel();
}
