#pragma once

#include <RE/Skyrim.h> // IWYU pragma: keep

#include "Equipment/FavoriteGroups.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Compatibility::SkyUI::FavoritesManager {
enum class Operation : std::uint8_t {
    kUse,
    kItem,
    kSave,
    kRemove,
};

struct Call {
    Operation operation {Operation::kUse};
    std::size_t group {0};
    std::vector<Equipment::FavoriteGroups::Member> members;
    std::vector<Equipment::FavoriteGroups::Row> rows;
    Equipment::FavoriteGroups::Member item;
    bool unequipArmor {false};
};

struct Outcome {
    bool handled {false};
    std::uint32_t outfitMask {0};
};

[[nodiscard]] bool IsManager(const RE::BSScript::IFunction& a_function);
[[nodiscard]] std::optional<Call> ReadCall(
    const RE::BSScript::IFunction& a_function,
    const RE::BSScript::StackFrame& a_frame
);
[[nodiscard]] Outcome Run(const Call& a_call);
void Complete(RE::BSScript::Stack& a_stack, const Outcome& a_outcome);
}
