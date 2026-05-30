#pragma once

#include "RingTargets.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace FingerSelectMenu {
inline constexpr std::size_t kRowCount = 5;

struct Row {
    RingTarget target;
    std::string fingerLabel;
    std::string equippedRingLabel;
    std::string actionLabel;
    std::uint16_t previewTargetBits {0};
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

enum class ResultDisposition : std::uint8_t {
    kClose,
    kKeepOpen,
};

using ResultCallback = std::function<ResultDisposition(Result)>;

struct Labels {
    Labels() = delete;

    Labels(
        std::string a_title,
        std::string a_fingerHeader,
        std::string a_equippedHeader,
        std::string a_equipAction,
        std::string a_unequipAction,
        std::string a_replaceAction,
        std::string a_wontFitAction,
        std::string a_cancelAction
    )
        : title(std::move(a_title))
        , fingerHeader(std::move(a_fingerHeader))
        , equippedHeader(std::move(a_equippedHeader))
        , equipAction(std::move(a_equipAction))
        , unequipAction(std::move(a_unequipAction))
        , replaceAction(std::move(a_replaceAction))
        , wontFitAction(std::move(a_wontFitAction))
        , cancelAction(std::move(a_cancelAction)) {}

    std::string title;
    std::string fingerHeader;
    std::string equippedHeader;
    std::string equipAction;
    std::string unequipAction;
    std::string replaceAction;
    std::string wontFitAction;
    std::string cancelAction;
};

struct Data {
    enum class HostMenu : std::uint8_t {
        kInventory,
        kFavorites,
    };

    Labels labels;
    std::string ringName;
    std::array<Row, kRowCount> rows;
    std::string previewEmptyRingLabel {"-"};
    std::uint16_t previewSourceTargetBits {0};
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
