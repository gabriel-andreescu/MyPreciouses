#include "Settings.h"

#include <CLIBUtil/simpleINI.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <string_view>
#include <utility>

namespace {
constexpr auto kModName = "LeftHandRingsSKSE"sv;
constexpr auto kSettingsSection = "General";
constexpr auto kEnchantmentStrengthModeKey = "iEnchantmentStrengthMode";
constexpr auto kFixedEnchantmentStrengthKey = "iFixedEnchantmentStrengthPercent";
constexpr auto kAlwaysChooseFingerKey = "bAlwaysChooseFinger";

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

    return std::min(static_cast<std::uint32_t>(a_value), Settings::kDefaultEnchantmentStrengthPercent);
}

template <class T>
void GetValue(
    CSimpleIniA& a_ini,
    T& a_value,
    const char* a_section,
    const char* a_key,
    const char* a_comment,
    const char* a_delimiter = R"(|)"
) {
    clib_util::ini::get_value(a_ini, a_value, a_section, a_key, a_comment, a_delimiter);
}
}

void Settings::Load() {
    enchantmentStrengthMode_.store(EnchantmentStrengthMode::kFullStrength);
    fixedEnchantmentStrengthPercent_.store(kDefaultFixedEnchantmentStrengthPercent);
    alwaysChooseFinger_.store(false);

    (void)Reload();
}

Settings::ReloadResult Settings::Reload() {
    const auto defaultPath = std::filesystem::path {"Data/MCM/Config"} / kModName / "settings.ini";
    const auto userPath = std::filesystem::path {"Data/MCM/Settings"} / std::format("{}.ini", kModName);

    auto rawMode = static_cast<int>(EnchantmentStrengthMode::kFullStrength);
    auto rawFixedStrength = static_cast<int>(kDefaultFixedEnchantmentStrengthPercent);
    auto alwaysChooseFinger = false;

    const auto readSettings = [&rawMode, &rawFixedStrength, &alwaysChooseFinger](CSimpleIniA& a_ini) {
        GetValue(
            a_ini,
            rawMode,
            kSettingsSection,
            kEnchantmentStrengthModeKey,
            "; Ring enchantment strength mode.\n; 0 = Full strength, 1 = Fixed strength, 2 = Split strength.\n; Default: 0"
        );
        GetValue(
            a_ini,
            rawFixedStrength,
            kSettingsSection,
            kFixedEnchantmentStrengthKey,
            "; Fixed ring enchantment strength.\n; Valid range: 5-100.\n; Default: 50"
        );
        GetValue(
            a_ini,
            alwaysChooseFinger,
            kSettingsSection,
            kAlwaysChooseFingerKey,
            "; Always show the finger selection menu for ring equip actions.\n; Default: false"
        );
    };

    if (std::filesystem::exists(defaultPath)) {
        CSimpleIniA defaults;
        defaults.SetUnicode();
        (void)defaults.LoadFile(defaultPath.string().c_str());
        readSettings(defaults);
    }

    std::filesystem::create_directories(userPath.parent_path());

    CSimpleIniA user;
    user.SetUnicode();
    (void)user.LoadFile(userPath.string().c_str());
    readSettings(user);

    const auto mode = ClampStrengthMode(rawMode);
    const auto fixedStrength = ClampStrengthPercent(rawFixedStrength);

    if (std::cmp_not_equal(rawMode, std::to_underlying(mode))) {
        user.SetLongValue(kSettingsSection, kEnchantmentStrengthModeKey, static_cast<long>(std::to_underlying(mode)));
    }
    if (std::cmp_not_equal(rawFixedStrength, fixedStrength)) {
        user.SetLongValue(kSettingsSection, kFixedEnchantmentStrengthKey, static_cast<long>(fixedStrength));
    }

    const auto modeChanged = enchantmentStrengthMode_.exchange(mode) != mode;
    const auto fixedStrengthChanged = fixedEnchantmentStrengthPercent_.exchange(fixedStrength) != fixedStrength;
    const auto fingerSelectionChanged = alwaysChooseFinger_.exchange(alwaysChooseFinger) != alwaysChooseFinger;

    (void)user.SaveFile(userPath.string().c_str());

    logger::info(
        "Settings: loaded | path={} | enchantmentStrengthMode={} | fixedStrength={} | alwaysChooseFinger={}",
        userPath.string(),
        std::to_underlying(GetEnchantmentStrengthMode()),
        GetFixedEnchantmentStrengthPercent(),
        AlwaysChooseFinger()
    );

    return ReloadResult {
        .enchantmentStrengthChanged = modeChanged || fixedStrengthChanged,
        .fingerSelectionChanged = fingerSelectionChanged,
    };
}

EnchantmentStrengthMode Settings::GetEnchantmentStrengthMode() const {
    return enchantmentStrengthMode_.load();
}

std::uint32_t Settings::GetFixedEnchantmentStrengthPercent() const {
    return fixedEnchantmentStrengthPercent_.load();
}

bool Settings::AlwaysChooseFinger() const {
    return alwaysChooseFinger_.load();
}

float Settings::GetRingEnchantmentScale(const std::uint32_t a_enchantedRingCount) const {
    if (a_enchantedRingCount <= 1) {
        return 1.0F;
    }

    switch (GetEnchantmentStrengthMode()) {
        case EnchantmentStrengthMode::kFullStrength: return 1.0F;
        case EnchantmentStrengthMode::kFixedStrength:
            return static_cast<float>(GetFixedEnchantmentStrengthPercent())
                   / static_cast<float>(kDefaultEnchantmentStrengthPercent);
        case EnchantmentStrengthMode::kSplitStrength: return 1.0F / static_cast<float>(a_enchantedRingCount);
    }

    return 1.0F;
}
