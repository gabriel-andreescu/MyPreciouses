#pragma once

#include "RingTargets.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace FingerSelectMenu {
inline constexpr std::size_t kRowCount = 5;

struct Row {
    RingTarget target;
    std::string fingerLabel;
    std::string equippedRingLabel;
    bool enabled {true};
};

struct Result {
    enum class Action : std::uint8_t {
        kEquip,
        kCancel,
    };

    Action action {Action::kCancel};
    std::optional<RingTarget> target;
    std::size_t index {0};
};

using ResultCallback = std::function<void(Result)>;

struct Data {
    enum class HostMenu : std::uint8_t {
        kInventory,
        kFavorites,
    };

    std::string title;
    std::string ringName;
    std::string fingerHeader {"FINGER"};
    std::string equippedHeader {"EQUIPPED RING"};
    std::string equipLabel {"Equip"};
    std::string cancelLabel {"Cancel"};
    std::array<Row, kRowCount> rows;
    std::size_t selectedIndex {0};
    RE::INPUT_DEVICE inputDevice {RE::INPUT_DEVICE::kKeyboard};
    HostMenu hostMenu {HostMenu::kInventory};
    ResultCallback onResult;
};

[[nodiscard]] bool Show(Data a_data);
[[nodiscard]] bool IsOpen();
[[nodiscard]] bool IsPendingOrOpen();
void OnMenuClose(const RE::BSFixedString& a_menuName);
void Cancel();
}
