#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace RE {
class GFxMovie;
class GFxValue;
}

namespace UI::VanillaItemMenuControls {
struct ButtonArt {
    std::string pc;
    std::string xbox;
    std::string ps3;
};

[[nodiscard]] bool SupportsControllerButtonArt(std::uint32_t a_button);
[[nodiscard]] std::optional<std::vector<ButtonArt>> ReadFixedSlotButtonArt(
    const RE::GFxValue& a_itemMenu,
    std::uint32_t a_firstIndex,
    std::uint32_t a_count
);
[[nodiscard]] std::optional<ButtonArt> ResolveModifierArt(
    const RE::GFxValue& a_itemMenu,
    std::uint32_t a_keyboardMouseKey,
    std::uint32_t a_controllerButton
);
[[nodiscard]] bool TrySetFixedSlotButtonArt(
    RE::GFxMovie& a_movie,
    const RE::GFxValue& a_itemMenu,
    std::uint32_t a_firstIndex,
    std::span<const ButtonArt> a_buttonArt
);
[[nodiscard]] bool TryPrependFixedSlotButton(
    RE::GFxMovie& a_movie,
    const RE::GFxValue& a_itemMenu,
    const ButtonArt& a_buttonArt,
    std::string_view a_buttonText,
    std::span<const ButtonArt> a_shiftedButtonArt
);
}
