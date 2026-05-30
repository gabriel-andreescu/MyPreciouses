#pragma once

#include "Core/Target.h"

#include <string>
#include <string_view>

namespace Localization {
void Load(std::string_view a_name);
[[nodiscard]] std::string Translate(std::string_view a_key, std::string_view a_fallback);
[[nodiscard]] std::string TranslateFingerLabel(Core::Finger a_finger);
}
