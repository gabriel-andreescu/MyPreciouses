#include "Settings.h"

#include <CLIBUtil/simpleINI.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <string_view>
#include <utility>

#include <SKSE/InputMap.h>

namespace {
constexpr auto kModName = "LeftHandRingsSKSE"sv;
constexpr auto kConfigRoot = "Data/MCM/Config"sv;
constexpr auto kSettingsRoot = "Data/MCM/Settings"sv;
constexpr auto kSettingsSection = "General";
constexpr auto kExtraRingModeSettingKey = "iExtraRingMode";
constexpr auto kEnchantmentStrengthModeSettingKey = "iEnchantmentStrengthMode";
constexpr auto kFixedEnchantmentStrengthSettingKey = "iFixedEnchantmentStrengthPercent";
constexpr auto kAlwaysChooseFingerSettingKey = "bAlwaysChooseFinger";
constexpr auto kFingerSelectKeyboardModifierSettingKey = "iFingerSelectModifierKey";
constexpr auto kFingerSelectGamepadModifierSettingKey = "iFingerSelectModifierButton";
constexpr auto kDebugLoggingSettingKey = "bEnableDebugLogging";

struct RawSettings {
    int extraRingMode {static_cast<int>(std::to_underlying(ExtraRingMode::kFunctional))};
    int enchantmentStrengthMode {static_cast<int>(std::to_underlying(EnchantmentStrengthMode::kFullStrength))};
    int fixedStrengthPercent {static_cast<int>(Settings::kDefaultFixedEnchantmentStrengthPercent)};
    bool alwaysChooseFinger {false};
    int fingerSelectModifierKey {static_cast<int>(Settings::kDefaultFingerSelectModifierKey)};
    int fingerSelectModifierButton {static_cast<int>(Settings::kDefaultFingerSelectModifierButton)};
};

struct LoadedSettings {
    ExtraRingMode extraRingMode {ExtraRingMode::kFunctional};
    EnchantmentStrengthMode enchantmentStrengthMode {EnchantmentStrengthMode::kFullStrength};
    std::uint32_t fixedStrengthPercent {Settings::kDefaultFixedEnchantmentStrengthPercent};
    bool alwaysChooseFinger {false};
    std::uint32_t fingerSelectModifierKey {Settings::kDefaultFingerSelectModifierKey};
    std::uint32_t fingerSelectModifierButton {Settings::kDefaultFingerSelectModifierButton};
};

[[nodiscard]] std::filesystem::path DefaultSettingsPath() {
    return std::filesystem::path {kConfigRoot} / kModName / "settings.ini";
}

[[nodiscard]] std::filesystem::path UserSettingsPath() {
    return std::filesystem::path {kSettingsRoot} / std::format("{}.ini", kModName);
}

[[nodiscard]] ExtraRingMode ClampExtraRingMode(const int a_value) {
    switch (a_value) {
        case std::to_underlying(ExtraRingMode::kFunctional):
        case std::to_underlying(ExtraRingMode::kCosmetic):   return static_cast<ExtraRingMode>(a_value);
        default:                                             return ExtraRingMode::kFunctional;
    }
}

[[nodiscard]] EnchantmentStrengthMode ClampStrengthMode(const int a_value) {
    switch (a_value) {
        case std::to_underlying(EnchantmentStrengthMode::kFullStrength):
        case std::to_underlying(EnchantmentStrengthMode::kFixedStrength):
        case std::to_underlying(EnchantmentStrengthMode::kSplitStrength):
            return static_cast<EnchantmentStrengthMode>(a_value);
        default: return EnchantmentStrengthMode::kFullStrength;
    }
}

[[nodiscard]] std::uint32_t ClampStrengthPercent(const int a_value) {
    if (std::cmp_less(a_value, Settings::kMinimumEnchantmentStrengthPercent)) {
        return Settings::kMinimumEnchantmentStrengthPercent;
    }

    return std::min(static_cast<std::uint32_t>(a_value), Settings::kMaximumEnchantmentStrengthPercent);
}

[[nodiscard]] std::uint32_t ClampFingerSelectModifierKey(const int a_value) {
    if (a_value >= SKSE::InputMap::kMacro_KeyboardOffset && a_value < SKSE::InputMap::kMacro_MouseWheelOffset) {
        return static_cast<std::uint32_t>(a_value);
    }

    return Settings::kDefaultFingerSelectModifierKey;
}

[[nodiscard]] std::uint32_t ClampFingerSelectModifierButton(const int a_value) {
    if (a_value >= SKSE::InputMap::kMacro_GamepadOffset && a_value < SKSE::InputMap::kMaxMacros) {
        return static_cast<std::uint32_t>(a_value);
    }

    return Settings::kDefaultFingerSelectModifierButton;
}

void ReadSettings(CSimpleIniA& a_ini, RawSettings& a_settings) {
    clib_util::ini::get_value(
        a_ini,
        a_settings.extraRingMode,
        kSettingsSection,
        kExtraRingModeSettingKey,
        "; Extra ring mode.\n; Functional applies normal ring enchantments and scripts. Cosmetic only shows the ring model.\n; Applies only to extra rings. The vanilla right-hand index finger ring is unaffected.\n; 0 = Functional, 1 = Cosmetic.\n; Default: 0"
    );
    clib_util::ini::get_value(
        a_ini,
        a_settings.enchantmentStrengthMode,
        kSettingsSection,
        kEnchantmentStrengthModeSettingKey,
        "; Ring enchantment strength mode.\n; Full keeps normal strength. Fixed uses the chosen strength. Split divides 100% evenly between counted rings.\n; Only equipped rings with at least one non-zero-magnitude enchantment effect are counted, including the vanilla right-hand index finger.\n; 0 = Full strength, 1 = Fixed strength, 2 = Split strength.\n; Default: 0"
    );
    clib_util::ini::get_value(
        a_ini,
        a_settings.fixedStrengthPercent,
        kSettingsSection,
        kFixedEnchantmentStrengthSettingKey,
        "; Fixed ring enchantment strength.\n; Used by Fixed strength mode. Only equipped rings with at least one non-zero-magnitude enchantment effect are counted, including the vanilla right-hand index finger.\n; Valid range: 5-100.\n; Default: 50"
    );
    clib_util::ini::get_value(
        a_ini,
        a_settings.alwaysChooseFinger,
        kSettingsSection,
        kAlwaysChooseFingerSettingKey,
        "; Always show the finger selection menu whenever you use Equip or Left Equip on a ring without pressing a modifier key.\n; Default: false"
    );
    clib_util::ini::get_value(
        a_ini,
        a_settings.fingerSelectModifierKey,
        kSettingsSection,
        kFingerSelectKeyboardModifierSettingKey,
        "; Finger selection modifier for keyboard and mouse input.\n; Default: 42"
    );
    clib_util::ini::get_value(
        a_ini,
        a_settings.fingerSelectModifierButton,
        kSettingsSection,
        kFingerSelectGamepadModifierSettingKey,
        "; Finger selection modifier for controller input.\n; Default: 274"
    );
}

[[nodiscard]] RawSettings ReadSettingsFiles(CSimpleIniA& a_user, const std::filesystem::path& a_userPath) {
    RawSettings settings;

    const auto defaultPath = DefaultSettingsPath();
    if (std::filesystem::exists(defaultPath)) {
        CSimpleIniA defaults;
        defaults.SetUnicode();
        (void)defaults.LoadFile(defaultPath.string().c_str());
        ReadSettings(defaults, settings);
    }

    std::filesystem::create_directories(a_userPath.parent_path());

    a_user.SetUnicode();
    (void)a_user.LoadFile(a_userPath.string().c_str());
    ReadSettings(a_user, settings);

    return settings;
}

[[nodiscard]] LoadedSettings NormalizeSettings(const RawSettings& a_raw) {
    return LoadedSettings {
        .extraRingMode = ClampExtraRingMode(a_raw.extraRingMode),
        .enchantmentStrengthMode = ClampStrengthMode(a_raw.enchantmentStrengthMode),
        .fixedStrengthPercent = ClampStrengthPercent(a_raw.fixedStrengthPercent),
        .alwaysChooseFinger = a_raw.alwaysChooseFinger,
        .fingerSelectModifierKey = ClampFingerSelectModifierKey(a_raw.fingerSelectModifierKey),
        .fingerSelectModifierButton = ClampFingerSelectModifierButton(a_raw.fingerSelectModifierButton),
    };
}

void RepairUserSettings(CSimpleIniA& a_user, const RawSettings& a_raw, const LoadedSettings& a_loaded) {
    if (std::cmp_not_equal(a_raw.extraRingMode, std::to_underlying(a_loaded.extraRingMode))) {
        a_user.SetLongValue(
            kSettingsSection,
            kExtraRingModeSettingKey,
            static_cast<long>(std::to_underlying(a_loaded.extraRingMode))
        );
    }
    if (std::cmp_not_equal(a_raw.enchantmentStrengthMode, std::to_underlying(a_loaded.enchantmentStrengthMode))) {
        a_user.SetLongValue(
            kSettingsSection,
            kEnchantmentStrengthModeSettingKey,
            static_cast<long>(std::to_underlying(a_loaded.enchantmentStrengthMode))
        );
    }
    if (std::cmp_not_equal(a_raw.fixedStrengthPercent, a_loaded.fixedStrengthPercent)) {
        a_user.SetLongValue(
            kSettingsSection,
            kFixedEnchantmentStrengthSettingKey,
            static_cast<long>(a_loaded.fixedStrengthPercent)
        );
    }
    if (std::cmp_not_equal(a_raw.fingerSelectModifierKey, a_loaded.fingerSelectModifierKey)) {
        a_user.SetLongValue(
            kSettingsSection,
            kFingerSelectKeyboardModifierSettingKey,
            static_cast<long>(a_loaded.fingerSelectModifierKey)
        );
    }
    if (std::cmp_not_equal(a_raw.fingerSelectModifierButton, a_loaded.fingerSelectModifierButton)) {
        a_user.SetLongValue(
            kSettingsSection,
            kFingerSelectGamepadModifierSettingKey,
            static_cast<long>(a_loaded.fingerSelectModifierButton)
        );
    }
}
}

void Settings::Load() {
    extraRingMode_.store(ExtraRingMode::kFunctional);
    enchantmentStrengthMode_.store(EnchantmentStrengthMode::kFullStrength);
    fixedEnchantmentStrengthPercent_.store(kDefaultFixedEnchantmentStrengthPercent);
    alwaysChooseFinger_.store(false);
    fingerSelectModifierKey_.store(kDefaultFingerSelectModifierKey);
    fingerSelectModifierButton_.store(kDefaultFingerSelectModifierButton);

    (void)Reload();
}

bool Settings::ReadDebugLoggingEnabled() {
    auto enabled = false;
    for (const auto& path : {DefaultSettingsPath(), UserSettingsPath()}) {
        if (!std::filesystem::exists(path)) {
            continue;
        }

        CSimpleIniA ini;
        ini.SetUnicode();
        if (ini.LoadFile(path.string().c_str()) >= 0) {
            enabled = ini.GetBoolValue(kSettingsSection, kDebugLoggingSettingKey, enabled);
        }
    }

    return enabled;
}

Settings::ReloadResult Settings::Reload() {
    const auto userPath = UserSettingsPath();
    CSimpleIniA user;
    const auto raw = ReadSettingsFiles(user, userPath);
    const auto loaded = NormalizeSettings(raw);
    RepairUserSettings(user, raw, loaded);

    const auto extraRingModeChanged = extraRingMode_.exchange(loaded.extraRingMode) != loaded.extraRingMode;
    const auto enchantmentStrengthModeChanged = enchantmentStrengthMode_.exchange(loaded.enchantmentStrengthMode)
                                                != loaded.enchantmentStrengthMode;
    const auto fixedStrengthChanged = fixedEnchantmentStrengthPercent_.exchange(loaded.fixedStrengthPercent)
                                      != loaded.fixedStrengthPercent;
    const auto alwaysChooseFingerChanged = alwaysChooseFinger_.exchange(loaded.alwaysChooseFinger)
                                           != loaded.alwaysChooseFinger;
    const auto modifierKeyChanged = fingerSelectModifierKey_.exchange(loaded.fingerSelectModifierKey)
                                    != loaded.fingerSelectModifierKey;
    const auto modifierButtonChanged = fingerSelectModifierButton_.exchange(loaded.fingerSelectModifierButton)
                                       != loaded.fingerSelectModifierButton;

    (void)user.SaveFile(userPath.string().c_str());

    logger::info(
        "Settings: loaded | path={} | extraRingMode={} | enchantmentStrengthMode={} | fixedStrength={} | alwaysChooseFinger={} | fingerSelectModifierKey={} | fingerSelectModifierButton={}",
        userPath.string(),
        std::to_underlying(loaded.extraRingMode),
        std::to_underlying(loaded.enchantmentStrengthMode),
        loaded.fixedStrengthPercent,
        loaded.alwaysChooseFinger,
        loaded.fingerSelectModifierKey,
        loaded.fingerSelectModifierButton
    );

    return ReloadResult {
        .extraRingModeChanged = extraRingModeChanged,
        .enchantmentStrengthChanged = enchantmentStrengthModeChanged || fixedStrengthChanged,
        .fingerSelectionChanged = alwaysChooseFingerChanged || modifierKeyChanged || modifierButtonChanged,
    };
}

ExtraRingMode Settings::GetExtraRingMode() const {
    return extraRingMode_.load();
}

bool Settings::AlwaysChooseFinger() const {
    return alwaysChooseFinger_.load();
}

std::uint32_t Settings::GetFingerSelectModifierKey() const {
    return fingerSelectModifierKey_.load();
}

std::uint32_t Settings::GetFingerSelectModifierButton() const {
    return fingerSelectModifierButton_.load();
}

float Settings::GetRingEnchantmentScale(const std::uint32_t a_enchantedRingCount) const {
    if (GetExtraRingMode() == ExtraRingMode::kCosmetic) {
        return 1.0F;
    }

    if (a_enchantedRingCount <= 1) {
        return 1.0F;
    }

    switch (enchantmentStrengthMode_.load()) {
        case EnchantmentStrengthMode::kFullStrength: return 1.0F;
        case EnchantmentStrengthMode::kFixedStrength:
            return static_cast<float>(fixedEnchantmentStrengthPercent_.load())
                   / static_cast<float>(kMaximumEnchantmentStrengthPercent);
        case EnchantmentStrengthMode::kSplitStrength: return 1.0F / static_cast<float>(a_enchantedRingCount);
    }

    return 1.0F;
}
