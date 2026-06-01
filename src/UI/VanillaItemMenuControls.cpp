#include "UI/VanillaItemMenuControls.h"

#include "UI/Scaleform.h"

#include <REX/W32/DINPUT.h>
#include <SKSE/InputMap.h>

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace UI::VanillaItemMenuControls {
namespace {
    using namespace std::literals;

    constexpr auto kBottomBar = "BottomBar_mc";
    constexpr auto kBottomBarButtons = "Buttons";
    constexpr auto kButtonLabel = "label";
    constexpr auto kButtonPcArt = "PCArt";
    constexpr auto kButtonXboxArt = "XBoxArt";
    constexpr auto kButtonPs3Art = "PS3Art";
    constexpr auto kPlatform = "iPlatform";
    constexpr auto kPlatformPc = 0;
    constexpr auto kPlatformPcGamepad = 1;
    constexpr auto kPlatformXbox = 2;
    constexpr auto kPcArtNone = "None"sv;
    constexpr auto kXboxArtRightShoulder = "360_RB"sv;
    constexpr auto kPs3ArtRightTrigger = "PS3_RT"sv;

    [[nodiscard]] std::optional<std::string_view> GetKeyboardMouseButtonArt(const std::uint32_t a_keyCode) {
        if (a_keyCode
            >= SKSE::InputMap::kMacro_MouseButtonOffset
            && a_keyCode
            < SKSE::InputMap::kMacro_MouseWheelOffset) {
            static constexpr std::array<std::string_view, SKSE::InputMap::kMacro_NumMouseButtons> kMouseButtonArt {
                "Mouse1",
                "Mouse2",
                "Mouse3",
                "Mouse4",
                "Mouse5",
                "Mouse6",
                "Mouse7",
                "Mouse8",
            };

            return kMouseButtonArt[a_keyCode - SKSE::InputMap::kMacro_MouseButtonOffset];
        }

        using enum REX::W32::DIK;
        switch (a_keyCode) {
            case DIK_ESCAPE:   return "Esc"sv;
            case DIK_1:        return "1"sv;
            case DIK_2:        return "2"sv;
            case DIK_3:        return "3"sv;
            case DIK_4:        return "4"sv;
            case DIK_5:        return "5"sv;
            case DIK_6:        return "6"sv;
            case DIK_7:        return "7"sv;
            case DIK_8:        return "8"sv;
            case DIK_9:        return "9"sv;
            case DIK_0:        return "0"sv;
            case DIK_TAB:      return "Tab"sv;
            case DIK_Q:        return "Q"sv;
            case DIK_W:        return "W"sv;
            case DIK_E:        return "E"sv;
            case DIK_R:        return "R"sv;
            case DIK_T:        return "T"sv;
            case DIK_Y:        return "Y"sv;
            case DIK_U:        return "U"sv;
            case DIK_I:        return "I"sv;
            case DIK_O:        return "O"sv;
            case DIK_P:        return "P"sv;
            case DIK_RETURN:   return "Enter"sv;
            case DIK_LCONTROL: return "L-Ctrl"sv;
            case DIK_A:        return "A"sv;
            case DIK_S:        return "S"sv;
            case DIK_D:        return "D"sv;
            case DIK_F:        return "F"sv;
            case DIK_G:        return "G"sv;
            case DIK_H:        return "H"sv;
            case DIK_J:        return "J"sv;
            case DIK_K:        return "K"sv;
            case DIK_L:        return "L"sv;
            case DIK_LSHIFT:   return "L-Shift"sv;
            case DIK_Z:        return "Z"sv;
            case DIK_X:        return "X"sv;
            case DIK_C:        return "C"sv;
            case DIK_V:        return "V"sv;
            case DIK_B:        return "B"sv;
            case DIK_N:        return "N"sv;
            case DIK_M:        return "M"sv;
            case DIK_RSHIFT:   return "R-Shift"sv;
            case DIK_LALT:     return "L-Alt"sv;
            case DIK_SPACE:    return "Space"sv;
            case DIK_RCONTROL: return "R-Ctrl"sv;
            case DIK_RALT:     return "R-Alt"sv;
            default:           return std::nullopt;
        }
    }

    void SetButtonArtObject(RE::GFxMovie& a_movie, RE::GFxValue& a_value, const ButtonArt& a_art) {
        a_movie.CreateObject(std::addressof(a_value));

        RE::GFxValue pcArt;
        pcArt.SetString(a_art.pc);
        a_value.SetMember(kButtonPcArt, pcArt);

        RE::GFxValue xboxArt;
        xboxArt.SetString(a_art.xbox);
        a_value.SetMember(kButtonXboxArt, xboxArt);

        RE::GFxValue ps3Art;
        ps3Art.SetString(a_art.ps3);
        a_value.SetMember(kButtonPs3Art, ps3Art);
    }

    [[nodiscard]] std::optional<ButtonArt> ReadButtonArtObject(const RE::GFxValue& a_value) {
        if (!Scaleform::CanReadMembers(a_value)) {
            return std::nullopt;
        }

        auto pcArt = Scaleform::ReadStringMember(a_value, kButtonPcArt);
        auto xboxArt = Scaleform::ReadStringMember(a_value, kButtonXboxArt);
        auto ps3Art = Scaleform::ReadStringMember(a_value, kButtonPs3Art);
        if (!pcArt || !xboxArt || !ps3Art) {
            return std::nullopt;
        }

        return ButtonArt {
            .pc = std::move(*pcArt),
            .xbox = std::move(*xboxArt),
            .ps3 = std::move(*ps3Art),
        };
    }

    [[nodiscard]] std::optional<std::string> GetBottomBarButtonLabel(
        const RE::GFxValue& a_bottomBar,
        const std::uint32_t a_index
    ) {
        RE::GFxValue buttons;
        if (!a_bottomBar.GetMember(kBottomBarButtons, std::addressof(buttons)) || !buttons.IsArray()) {
            return std::nullopt;
        }

        RE::GFxValue button;
        if (!buttons.GetElement(a_index, std::addressof(button)) || !Scaleform::CanReadMembers(button)) {
            return std::nullopt;
        }

        return Scaleform::ReadStringMember(button, kButtonLabel);
    }

    [[nodiscard]] bool CollectShiftedButtonLabels(
        const RE::GFxValue& a_bottomBar,
        const std::uint32_t a_count,
        std::vector<std::string>& a_labels
    ) {
        a_labels.clear();
        a_labels.reserve(a_count);

        for (std::uint32_t index = 0; index < a_count; ++index) {
            const auto label = GetBottomBarButtonLabel(a_bottomBar, index);
            if (!label) {
                return false;
            }

            a_labels.push_back(*label);
        }

        return true;
    }
}

bool SupportsControllerButtonArt(const std::uint32_t a_button) {
    return a_button == SKSE::InputMap::kGamepadButtonOffset_RIGHT_SHOULDER;
}

std::optional<std::vector<ButtonArt>> ReadFixedSlotButtonArt(
    const RE::GFxValue& a_itemMenu,
    const std::uint32_t a_firstIndex,
    const std::uint32_t a_count
) {
    RE::GFxValue bottomBar;
    if (!a_itemMenu.GetMember(kBottomBar, std::addressof(bottomBar)) || !Scaleform::CanReadMembers(bottomBar)) {
        return std::nullopt;
    }

    RE::GFxValue currentArt;
    if (!bottomBar.Invoke("GetButtonsArt", std::addressof(currentArt)) || !currentArt.IsArray()) {
        return std::nullopt;
    }

    const auto buttonCount = currentArt.GetArraySize();
    if (a_firstIndex > buttonCount || a_count > buttonCount - a_firstIndex) {
        return std::nullopt;
    }

    std::vector<ButtonArt> result;
    result.reserve(a_count);
    for (std::uint32_t index = 0; index < a_count; ++index) {
        RE::GFxValue art;
        if (!currentArt.GetElement(a_firstIndex + index, std::addressof(art))) {
            return std::nullopt;
        }

        auto buttonArt = ReadButtonArtObject(art);
        if (!buttonArt) {
            return std::nullopt;
        }

        result.push_back(std::move(*buttonArt));
    }

    return result;
}

std::optional<ButtonArt> ResolveModifierArt(
    const RE::GFxValue& a_itemMenu,
    const std::uint32_t a_keyboardMouseKey,
    const std::uint32_t a_controllerButton
) {
    const auto platform = Scaleform::ReadIntMember(a_itemMenu, kPlatform);
    if (!platform) {
        return std::nullopt;
    }

    if (*platform == kPlatformPc) {
        const auto pcArt = GetKeyboardMouseButtonArt(a_keyboardMouseKey);
        if (!pcArt) {
            return std::nullopt;
        }

        return ButtonArt {
            .pc = std::string {*pcArt},
            .xbox = std::string {kXboxArtRightShoulder},
            .ps3 = std::string {kPs3ArtRightTrigger},
        };
    }

    const auto controllerPlatform = *platform == kPlatformPcGamepad || *platform == kPlatformXbox;
    if (!controllerPlatform || !SupportsControllerButtonArt(a_controllerButton)) {
        return std::nullopt;
    }

    return ButtonArt {
        .pc = std::string {kPcArtNone},
        .xbox = std::string {kXboxArtRightShoulder},
        .ps3 = std::string {kPs3ArtRightTrigger},
    };
}

bool TrySetFixedSlotButtonArt(
    RE::GFxMovie& a_movie,
    const RE::GFxValue& a_itemMenu,
    const std::uint32_t a_firstIndex,
    const std::span<const ButtonArt> a_buttonArt
) {
    if (a_buttonArt.empty()) {
        return false;
    }

    RE::GFxValue bottomBar;
    if (!a_itemMenu.GetMember(kBottomBar, std::addressof(bottomBar)) || !Scaleform::CanReadMembers(bottomBar)) {
        return false;
    }

    for (std::size_t index = 0; index < a_buttonArt.size(); ++index) {
        RE::GFxValue art;
        SetButtonArtObject(a_movie, art, a_buttonArt[index]);

        std::array<RE::GFxValue, 2> args {
            art,
            RE::GFxValue {static_cast<double>(a_firstIndex + static_cast<std::uint32_t>(index))}
        };
        if (!bottomBar.Invoke("SetButtonArt", nullptr, args)) {
            return false;
        }
    }

    return true;
}

bool TryPrependFixedSlotButton(
    RE::GFxMovie& a_movie,
    const RE::GFxValue& a_itemMenu,
    const ButtonArt& a_buttonArt,
    const std::string_view a_buttonText,
    const std::span<const ButtonArt> a_shiftedButtonArt
) {
    if (a_shiftedButtonArt.empty()) {
        return false;
    }

    RE::GFxValue bottomBar;
    if (!a_itemMenu.GetMember(kBottomBar, std::addressof(bottomBar)) || !Scaleform::CanReadMembers(bottomBar)) {
        return false;
    }

    std::vector<std::string> shiftedLabels;
    const auto shiftedButtonCount = static_cast<std::uint32_t>(a_shiftedButtonArt.size());
    if (!CollectShiftedButtonLabels(bottomBar, shiftedButtonCount, shiftedLabels)) {
        return false;
    }

    RE::GFxValue leadingArt;
    SetButtonArtObject(a_movie, leadingArt, a_buttonArt);

    RE::GFxValue buttonArt;
    a_movie.CreateArray(std::addressof(buttonArt));
    buttonArt.SetElement(0, leadingArt);
    for (std::uint32_t index = 0; index < shiftedButtonCount; ++index) {
        RE::GFxValue art;
        SetButtonArtObject(a_movie, art, a_shiftedButtonArt[index]);
        buttonArt.SetElement(index + 1, art);
    }

    std::array<RE::GFxValue, 1> artArgs {buttonArt};
    if (!bottomBar.Invoke("SetButtonsArt", nullptr, artArgs)) {
        return false;
    }

    std::vector<RE::GFxValue> textArgs;
    textArgs.reserve(a_shiftedButtonArt.size() + 1);

    RE::GFxValue leadingText;
    leadingText.SetString(a_buttonText);
    textArgs.push_back(leadingText);

    for (const auto& label : shiftedLabels) {
        RE::GFxValue text;
        text.SetString(label);
        textArgs.push_back(text);
    }

    return bottomBar.Invoke("SetButtonsText", nullptr, textArgs.data(), textArgs.size());
}
}
