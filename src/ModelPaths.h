#pragma once

#include <cstring>
#include <ranges>
#include <string>
#include <vector>

namespace ModelPaths {
[[nodiscard]] inline const char* Get(const RE::TESModel& a_model) {
    const auto* model = a_model.GetModel();
    return model && model[0] != '\0' ? model : nullptr;
}

inline void AddUnique(std::vector<std::string>& a_paths, const RE::TESModel& a_model) {
    const auto* path = Get(a_model);
    if (!path) {
        return;
    }

    if (std::ranges::none_of(a_paths, [path](const auto& a_existing) {
            return _stricmp(a_existing.c_str(), path) == 0;
        })) {
        a_paths.emplace_back(path);
    }
}
}
