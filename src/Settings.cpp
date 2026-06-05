#include "Settings.h"

#include <CLIBUtil/simpleINI.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <string_view>
#include <utility>

#include <SKSE/InputMap.h>

namespace {
constexpr auto kModName = "LeftHandRingsSKSE"sv;
constexpr auto kConfigRoot = "Data/MCM/Config"sv;
constexpr auto kSettingsRoot = "Data/MCM/Settings"sv;
constexpr auto kGeneralSection = "General";
constexpr auto kFingerSelectorSection = "FingerSelector";
constexpr auto kVirtualSlotsSection = "VirtualSlots";
constexpr auto kExtraRingModeSettingKey = "iExtraRingMode";
constexpr auto kEnchantmentStrengthModeSettingKey = "iEnchantmentStrengthMode";
constexpr auto kFixedEnchantmentStrengthSettingKey = "iFixedEnchantmentStrengthPercent";
constexpr auto kUnequipAllClearsExtraRingsSettingKey = "bUnequipAllClearsExtraRings";
constexpr auto kAlwaysChooseFingerSettingKey = "bAlwaysChooseFinger";
constexpr auto kFingerSelectKeyboardModifierSettingKey = "iFingerSelectModifierKey";
constexpr auto kFingerSelectGamepadModifierSettingKey = "iFingerSelectModifierButton";
constexpr auto kDebugLoggingSettingKey = "bEnableDebugLogging";
constexpr auto kUnequipAllClearsExtraRingsSettingComment
    = "; Clear extra rings when UnequipAll runs, then restore them after race transformations such as werewolf or vampire lord\n; Default: 1";
constexpr auto kAlwaysChooseFingerSettingComment
    = "; Always show the finger selection menu whenever you use Equip or Left Equip on a ring without pressing a modifier key.\n; Default: 0";
constexpr auto kFingerSelectKeyboardModifierSettingComment
    = "; Finger selection modifier for keyboard and mouse input.\n; Default: 42";
constexpr auto kFingerSelectGamepadModifierSettingComment
    = "; Finger selection modifier for controller input.\n; Vanilla UI inventory hints only support RB. The finger selector still works, but the inventory hint will not be shown for other controller buttons.\n; Default: 275";

void SetMcmHelperBoolValue(
    CSimpleIniA& a_ini,
    const char* a_section,
    const char* a_key,
    const bool a_value,
    const char* a_comment = nullptr
) {
    a_ini.SetLongValue(a_section, a_key, a_value ? 1L : 0L, a_comment);
}

void ReadMcmHelperBoolValue(
    CSimpleIniA& a_ini,
    bool& a_value,
    const char* a_section,
    const char* a_key,
    const char* a_comment
) {
    const auto* existing = a_ini.GetValue(a_section, a_key, nullptr);
    a_value = a_ini.GetBoolValue(a_section, a_key, a_value);
    if (!existing) {
        SetMcmHelperBoolValue(a_ini, a_section, a_key, a_value, a_comment);
    }
}

struct VirtualSlotSetting {
    Core::Target target;
    const char* key;
    const char* comment;
};

constexpr std::array kVirtualSlotSettings {
    VirtualSlotSetting {
        .target = Core::Target {.hand = Core::Hand::kLeft, .finger = Core::Finger::kThumb},
        .key = "bEnableLeftThumb",
        .comment = "; Enable the left thumb virtual ring slot.\n; Default: 1",
    },
    VirtualSlotSetting {
        .target = Core::Target {.hand = Core::Hand::kLeft, .finger = Core::Finger::kIndex},
        .key = "bEnableLeftIndex",
        .comment = "; Enable the left index virtual ring slot.\n; Default: 1",
    },
    VirtualSlotSetting {
        .target = Core::Target {.hand = Core::Hand::kLeft, .finger = Core::Finger::kMiddle},
        .key = "bEnableLeftMiddle",
        .comment = "; Enable the left middle virtual ring slot.\n; Default: 1",
    },
    VirtualSlotSetting {
        .target = Core::Target {.hand = Core::Hand::kLeft, .finger = Core::Finger::kRing},
        .key = "bEnableLeftRing",
        .comment = "; Enable the left ring virtual ring slot.\n; Default: 1",
    },
    VirtualSlotSetting {
        .target = Core::Target {.hand = Core::Hand::kLeft, .finger = Core::Finger::kPinky},
        .key = "bEnableLeftPinky",
        .comment = "; Enable the left pinky virtual ring slot.\n; Default: 1",
    },
    VirtualSlotSetting {
        .target = Core::Target {.hand = Core::Hand::kRight, .finger = Core::Finger::kThumb},
        .key = "bEnableRightThumb",
        .comment = "; Enable the right thumb virtual ring slot.\n; Default: 1",
    },
    VirtualSlotSetting {
        .target = Core::Target {.hand = Core::Hand::kRight, .finger = Core::Finger::kMiddle},
        .key = "bEnableRightMiddle",
        .comment = "; Enable the right middle virtual ring slot.\n; Default: 1",
    },
    VirtualSlotSetting {
        .target = Core::Target {.hand = Core::Hand::kRight, .finger = Core::Finger::kRing},
        .key = "bEnableRightRing",
        .comment = "; Enable the right ring virtual ring slot.\n; Default: 1",
    },
    VirtualSlotSetting {
        .target = Core::Target {.hand = Core::Hand::kRight, .finger = Core::Finger::kPinky},
        .key = "bEnableRightPinky",
        .comment = "; Enable the right pinky virtual ring slot.\n; Default: 1",
    },
};

constexpr std::array kDefaultLeftTargetPriority {
    Core::Target {.hand = Core::Hand::kLeft, .finger = Core::Finger::kIndex},
    Core::Target {.hand = Core::Hand::kLeft, .finger = Core::Finger::kMiddle},
    Core::Target {.hand = Core::Hand::kLeft, .finger = Core::Finger::kRing},
    Core::Target {.hand = Core::Hand::kLeft, .finger = Core::Finger::kPinky},
    Core::Target {.hand = Core::Hand::kLeft, .finger = Core::Finger::kThumb},
};

using VirtualSlotStates = std::array<bool, Core::kAllTargets.size()>;

[[nodiscard]] constexpr VirtualSlotStates AllVirtualSlots() {
    VirtualSlotStates slots {};
    for (const auto target : Core::kVirtualTargets) {
        slots[Core::ToIndex(target)] = true;
    }
    return slots;
}

struct RawSettings {
    int extraRingMode {static_cast<int>(std::to_underlying(ExtraRingMode::kFunctional))};
    int enchantmentStrengthMode {static_cast<int>(std::to_underlying(EnchantmentStrengthMode::kFullStrength))};
    int fixedStrengthPercent {static_cast<int>(Settings::kDefaultFixedEnchantmentStrengthPercent)};
    bool unequipAllClearsExtraRings {true};
    bool alwaysChooseFinger {false};
    int fingerSelectModifierKey {static_cast<int>(Settings::kDefaultFingerSelectModifierKey)};
    int fingerSelectModifierButton {static_cast<int>(Settings::kDefaultFingerSelectModifierButton)};
    VirtualSlotStates virtualSlots = AllVirtualSlots();
};

struct LoadedSettings {
    ExtraRingMode extraRingMode {ExtraRingMode::kFunctional};
    EnchantmentStrengthMode enchantmentStrengthMode {EnchantmentStrengthMode::kFullStrength};
    std::uint32_t fixedStrengthPercent {Settings::kDefaultFixedEnchantmentStrengthPercent};
    bool unequipAllClearsExtraRings {true};
    bool alwaysChooseFinger {false};
    std::uint32_t fingerSelectModifierKey {Settings::kDefaultFingerSelectModifierKey};
    std::uint32_t fingerSelectModifierButton {Settings::kDefaultFingerSelectModifierButton};
    std::uint16_t enabledVirtualTargetBits {Settings::kDefaultEnabledVirtualTargetBits};
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

[[nodiscard]] std::uint16_t ToTargetBits(const VirtualSlotStates& a_slots) {
    auto bits = std::uint16_t {0};
    for (const auto target : Core::kVirtualTargets) {
        if (a_slots[Core::ToIndex(target)]) {
            bits |= static_cast<std::uint16_t>(1u << Core::ToIndex(target));
        }
    }
    return bits;
}

[[nodiscard]] bool IsTargetEnabled(const std::uint16_t a_enabledVirtualTargetBits, const Core::Target a_target) {
    if (!Core::IsVirtualTarget(a_target)) {
        return true;
    }

    return (a_enabledVirtualTargetBits & static_cast<std::uint16_t>(1u << Core::ToIndex(a_target))) != 0;
}

void MigrateLegacyFingerSelectorSetting(CSimpleIniA& a_ini, const char* a_key, const char* a_comment) {
    const auto* legacyValue = a_ini.GetValue(kGeneralSection, a_key, nullptr);
    if (!legacyValue) {
        return;
    }

    if (!a_ini.KeyExists(kFingerSelectorSection, a_key)) {
        a_ini.SetValue(kFingerSelectorSection, a_key, legacyValue, a_comment);
    }

    (void)a_ini.Delete(kGeneralSection, a_key, false);
}

void MigrateLegacyFingerSelectorSettings(CSimpleIniA& a_ini) {
    MigrateLegacyFingerSelectorSetting(a_ini, kAlwaysChooseFingerSettingKey, kAlwaysChooseFingerSettingComment);
    MigrateLegacyFingerSelectorSetting(
        a_ini,
        kFingerSelectKeyboardModifierSettingKey,
        kFingerSelectKeyboardModifierSettingComment
    );
    MigrateLegacyFingerSelectorSetting(
        a_ini,
        kFingerSelectGamepadModifierSettingKey,
        kFingerSelectGamepadModifierSettingComment
    );
}

void ReadSettings(CSimpleIniA& a_ini, RawSettings& a_settings) {
    clib_util::ini::get_value(
        a_ini,
        a_settings.extraRingMode,
        kGeneralSection,
        kExtraRingModeSettingKey,
        "; Extra ring mode.\n; Functional applies normal ring enchantments and scripts. Cosmetic only shows the ring model.\n; Applies only to extra rings. The vanilla right-hand index finger ring is unaffected.\n; 0 = Functional, 1 = Cosmetic.\n; Default: 0"
    );
    clib_util::ini::get_value(
        a_ini,
        a_settings.enchantmentStrengthMode,
        kGeneralSection,
        kEnchantmentStrengthModeSettingKey,
        "; Ring enchantment strength mode.\n; Full keeps normal strength. Fixed uses the chosen strength. Split divides 100% evenly between counted rings.\n; Only equipped rings with at least one non-zero-magnitude enchantment effect are counted, including the vanilla right-hand index finger.\n; 0 = Full strength, 1 = Fixed strength, 2 = Split strength.\n; Default: 0"
    );
    clib_util::ini::get_value(
        a_ini,
        a_settings.fixedStrengthPercent,
        kGeneralSection,
        kFixedEnchantmentStrengthSettingKey,
        "; Fixed ring enchantment strength.\n; Used by Fixed strength mode. Only equipped rings with at least one non-zero-magnitude enchantment effect are counted, including the vanilla right-hand index finger.\n; Valid range: 5-100.\n; Default: 50"
    );
    ReadMcmHelperBoolValue(
        a_ini,
        a_settings.unequipAllClearsExtraRings,
        kGeneralSection,
        kUnequipAllClearsExtraRingsSettingKey,
        kUnequipAllClearsExtraRingsSettingComment
    );
    ReadMcmHelperBoolValue(
        a_ini,
        a_settings.alwaysChooseFinger,
        kFingerSelectorSection,
        kAlwaysChooseFingerSettingKey,
        kAlwaysChooseFingerSettingComment
    );
    clib_util::ini::get_value(
        a_ini,
        a_settings.fingerSelectModifierKey,
        kFingerSelectorSection,
        kFingerSelectKeyboardModifierSettingKey,
        kFingerSelectKeyboardModifierSettingComment
    );
    clib_util::ini::get_value(
        a_ini,
        a_settings.fingerSelectModifierButton,
        kFingerSelectorSection,
        kFingerSelectGamepadModifierSettingKey,
        kFingerSelectGamepadModifierSettingComment
    );
    for (const auto& setting : kVirtualSlotSettings) {
        ReadMcmHelperBoolValue(
            a_ini,
            a_settings.virtualSlots[Core::ToIndex(setting.target)],
            kVirtualSlotsSection,
            setting.key,
            setting.comment
        );
    }
}

[[nodiscard]] RawSettings ReadSettingsFiles(CSimpleIniA& a_user, const std::filesystem::path& a_userPath) {
    RawSettings settings;

    const auto defaultPath = DefaultSettingsPath();
    if (std::filesystem::exists(defaultPath)) {
        CSimpleIniA defaults;
        defaults.SetUnicode();
        (void)defaults.LoadFile(defaultPath.string().c_str());
        MigrateLegacyFingerSelectorSettings(defaults);
        ReadSettings(defaults, settings);
    }

    std::filesystem::create_directories(a_userPath.parent_path());

    a_user.SetUnicode();
    (void)a_user.LoadFile(a_userPath.string().c_str());
    MigrateLegacyFingerSelectorSettings(a_user);
    ReadSettings(a_user, settings);

    return settings;
}

[[nodiscard]] LoadedSettings NormalizeSettings(const RawSettings& a_raw) {
    return LoadedSettings {
        .extraRingMode = ClampExtraRingMode(a_raw.extraRingMode),
        .enchantmentStrengthMode = ClampStrengthMode(a_raw.enchantmentStrengthMode),
        .fixedStrengthPercent = ClampStrengthPercent(a_raw.fixedStrengthPercent),
        .unequipAllClearsExtraRings = a_raw.unequipAllClearsExtraRings,
        .alwaysChooseFinger = a_raw.alwaysChooseFinger,
        .fingerSelectModifierKey = ClampFingerSelectModifierKey(a_raw.fingerSelectModifierKey),
        .fingerSelectModifierButton = ClampFingerSelectModifierButton(a_raw.fingerSelectModifierButton),
        .enabledVirtualTargetBits = ToTargetBits(a_raw.virtualSlots),
    };
}

void RepairUserSettings(CSimpleIniA& a_user, const RawSettings& a_raw, const LoadedSettings& a_loaded) {
    if (std::cmp_not_equal(a_raw.extraRingMode, std::to_underlying(a_loaded.extraRingMode))) {
        a_user.SetLongValue(
            kGeneralSection,
            kExtraRingModeSettingKey,
            static_cast<long>(std::to_underlying(a_loaded.extraRingMode))
        );
    }
    if (std::cmp_not_equal(a_raw.enchantmentStrengthMode, std::to_underlying(a_loaded.enchantmentStrengthMode))) {
        a_user.SetLongValue(
            kGeneralSection,
            kEnchantmentStrengthModeSettingKey,
            static_cast<long>(std::to_underlying(a_loaded.enchantmentStrengthMode))
        );
    }
    if (std::cmp_not_equal(a_raw.fixedStrengthPercent, a_loaded.fixedStrengthPercent)) {
        a_user.SetLongValue(
            kGeneralSection,
            kFixedEnchantmentStrengthSettingKey,
            static_cast<long>(a_loaded.fixedStrengthPercent)
        );
    }
    if (std::cmp_not_equal(a_raw.fingerSelectModifierKey, a_loaded.fingerSelectModifierKey)) {
        a_user.SetLongValue(
            kFingerSelectorSection,
            kFingerSelectKeyboardModifierSettingKey,
            static_cast<long>(a_loaded.fingerSelectModifierKey)
        );
    }
    if (std::cmp_not_equal(a_raw.fingerSelectModifierButton, a_loaded.fingerSelectModifierButton)) {
        a_user.SetLongValue(
            kFingerSelectorSection,
            kFingerSelectGamepadModifierSettingKey,
            static_cast<long>(a_loaded.fingerSelectModifierButton)
        );
    }
    for (const auto& setting : kVirtualSlotSettings) {
        const auto targetIndex = Core::ToIndex(setting.target);
        const auto loaded = ::IsTargetEnabled(a_loaded.enabledVirtualTargetBits, setting.target);
        if (a_raw.virtualSlots[targetIndex] != loaded) {
            SetMcmHelperBoolValue(a_user, kVirtualSlotsSection, setting.key, loaded);
        }
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
    unequipAllClearsExtraRings_.store(true);
    enabledVirtualTargetBits_.store(kDefaultEnabledVirtualTargetBits);

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
            enabled = ini.GetBoolValue(kGeneralSection, kDebugLoggingSettingKey, enabled);
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
    const auto unequipAllClearsExtraRingsChanged = unequipAllClearsExtraRings_.exchange(
                                                       loaded.unequipAllClearsExtraRings
                                                   )
                                                   != loaded.unequipAllClearsExtraRings;
    const auto virtualSlotsChanged = enabledVirtualTargetBits_.exchange(loaded.enabledVirtualTargetBits)
                                     != loaded.enabledVirtualTargetBits;

    (void)user.SaveFile(userPath.string().c_str());

    logger::info(
        "Settings: loaded | path={} | extraRingMode={} | enchantmentStrengthMode={} | fixedStrength={} | unequipAllClearsExtraRings={} | alwaysChooseFinger={} | fingerSelectModifierKey={} | fingerSelectModifierButton={} | enabledVirtualTargets={:04X}",
        userPath.string(),
        std::to_underlying(loaded.extraRingMode),
        std::to_underlying(loaded.enchantmentStrengthMode),
        loaded.fixedStrengthPercent,
        loaded.unequipAllClearsExtraRings,
        loaded.alwaysChooseFinger,
        loaded.fingerSelectModifierKey,
        loaded.fingerSelectModifierButton,
        loaded.enabledVirtualTargetBits
    );

    return ReloadResult {
        .extraRingModeChanged = extraRingModeChanged,
        .enchantmentStrengthChanged = enchantmentStrengthModeChanged || fixedStrengthChanged,
        .fingerSelectionChanged = alwaysChooseFingerChanged || modifierKeyChanged || modifierButtonChanged,
        .unequipAllClearsExtraRingsChanged = unequipAllClearsExtraRingsChanged,
        .unequipAllClearsExtraRingsEnabled = loaded.unequipAllClearsExtraRings,
        .virtualSlotsChanged = virtualSlotsChanged,
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

bool Settings::ShouldUnequipAllClearExtraRings() const {
    return unequipAllClearsExtraRings_.load();
}

bool Settings::IsTargetEnabled(const Core::Target a_target) const {
    return ::IsTargetEnabled(enabledVirtualTargetBits_.load(), a_target);
}

bool Settings::AreTargetsEnabled(const Core::TargetMask& a_targets) const {
    const auto enabledVirtualTargetBits = enabledVirtualTargetBits_.load();
    return std::ranges::all_of(Core::kVirtualTargets, [&](const auto target) {
        return !a_targets.Contains(target) || ::IsTargetEnabled(enabledVirtualTargetBits, target);
    });
}

std::optional<Core::Target> Settings::GetDefaultLeftTarget() const {
    const auto enabledVirtualTargetBits = enabledVirtualTargetBits_.load();
    for (const auto target : kDefaultLeftTargetPriority) {
        if (::IsTargetEnabled(enabledVirtualTargetBits, target)) {
            return target;
        }
    }

    return std::nullopt;
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
