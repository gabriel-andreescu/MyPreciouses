#pragma once

#include <vector>

namespace Papyrus::FormScriptNames {
[[nodiscard]] std::vector<RE::BSFixedString> GetRecordScriptNames(const RE::TESForm& a_form);
}
