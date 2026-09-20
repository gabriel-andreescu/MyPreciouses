#include "Settings.h"
#include "Core/ActorKey.h"
#include "Core/Target.h"
#include "Core/TargetMask.h"

#include <SKSE/SKSE.h> // IWYU pragma: keep

#include <BMK/Settings.h>
#include <ClibUtil/simpleINI.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string_view>
#include <utility>

#include <SKSE/InputMap.h>

namespace {
constexpr auto kModName = std::string_view {"MyPreciouses"};
constexpr auto kConfigRoot = std::string_view {"Data/MCM/Config"};
constexpr auto kSettingsRoot = std::string_view {"Data/MCM/Settings"};
constexpr auto kGeneralSection = "General";
constexpr auto kNpcSection = "NPCs";
constexpr auto kFingerSelectorSection = "FingerSelector";
constexpr auto kSpecialRingsSection = "SpecialRings";
constexpr auto kVirtualSlotsSection = "VirtualSlots";
constexpr auto kExtraRingModeSettingKey = "iExtraRingMode";
constexpr auto kEnchantmentStrengthModeSettingKey = "iEnchantmentStrengthMode";
constexpr auto kFixedEnchantmentStrengthSettingKey = "iFixedEnchantmentStrengthPercent";
constexpr auto kEnableNpcSupportSettingKey = "bEnableNpcSupport";
constexpr auto
    kPlayerAlwaysEquipBondOfMatrimonyOnLeftRingFingerSettingKey = "bPlayerAlwaysEquipBondOfMatrimonyOnLeftRingFinger";
constexpr auto
    kNpcAlwaysEquipBondOfMatrimonyOnLeftRingFingerSettingKey = "bNpcAlwaysEquipBondOfMatrimonyOnLeftRingFinger";
constexpr auto kUnequipAllClearsExtraRingsSettingKey = "bUnequipAllClearsExtraRings";
constexpr auto kAlwaysChooseFingerSettingKey = "bAlwaysChooseFinger";
constexpr auto kFingerSelectKeyboardModifierSettingKey = "iFingerSelectModifierKey";
constexpr auto kFingerSelectGamepadModifierSettingKey = "iFingerSelectModifierButton";
constexpr auto kDebugLoggingSettingKey = "bDebugLogging";
constexpr auto kUnequipAllClearsExtraRingsSettingComment
    = "; Clear extra rings when UnequipAll runs, then restore them after race transformations such as werewolf or vampire lord\n; Default: 1";
constexpr auto kEnableNpcSupportSettingComment
    = "; Enable virtual ring support for actors other than the player, including followers and generic NPCs.\n; Default: 1";
constexpr auto kPlayerAlwaysEquipBondOfMatrimonyOnLeftRingFingerSettingComment
    = "; Always equip The Bond of Matrimony and its Upgradable Bond variants on the player's left ring finger. This works even when that virtual slot is disabled.\n; Default: 0";
constexpr auto kNpcAlwaysEquipBondOfMatrimonyOnLeftRingFingerSettingComment
    = "; Always equip The Bond of Matrimony and its Upgradable Bond variants on the left ring finger for NPCs, including followers, when NPC support auto-equips rings. This works even when that virtual slot is disabled.\n; Default: 1";
constexpr auto kAlwaysChooseFingerSettingComment
    = "; Always show the finger selection menu whenever you use Equip or Left Equip on a ring without pressing a modifier key.\n; Default: 0";
constexpr auto kFingerSelectKeyboardModifierSettingComment
    = "; Finger selection modifier for keyboard and mouse input.\n; Default: 42";
constexpr auto kFingerSelectGamepadModifierSettingComment
    = "; Finger selection modifier for controller input.\n; Vanilla UI inventory hints only support RB. The finger selector still works, but the inventory hint will not be shown for other controller buttons.\n; Default: 275";

struct VirtualSlotSetting {
    Core::Target target;
    const char* key {};
    const char* comment {};
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
    bool debugLoggingEnabled {false};
    int extraRingMode {static_cast<int>(std::to_underlying(ExtraRingMode::kFunctional))};
    int enchantmentStrengthMode {static_cast<int>(std::to_underlying(EnchantmentStrengthMode::kFullStrength))};
    int fixedStrengthPercent {static_cast<int>(Settings::kDefaultFixedEnchantmentStrengthPercent)};
    bool npcSupportEnabled {true};
    bool playerAlwaysEquipBondOfMatrimonyOnLeftRingFinger {false};
    bool npcAlwaysEquipBondOfMatrimonyOnLeftRingFinger {true};
    bool unequipAllClearsExtraRings {true};
    bool alwaysChooseFinger {false};
    int fingerSelectModifierKey {static_cast<int>(Settings::kDefaultFingerSelectModifierKey)};
    int fingerSelectModifierButton {static_cast<int>(Settings::kDefaultFingerSelectModifierButton)};
    VirtualSlotStates virtualSlots = AllVirtualSlots();
};

struct LoadedSettings {
    bool debugLoggingEnabled {false};
    ExtraRingMode extraRingMode {ExtraRingMode::kFunctional};
    EnchantmentStrengthMode enchantmentStrengthMode {EnchantmentStrengthMode::kFullStrength};
    std::uint32_t fixedStrengthPercent {Settings::kDefaultFixedEnchantmentStrengthPercent};
    bool npcSupportEnabled {true};
    bool playerAlwaysEquipBondOfMatrimonyOnLeftRingFinger {false};
    bool npcAlwaysEquipBondOfMatrimonyOnLeftRingFinger {true};
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
            bits |= static_cast<std::uint16_t>(1U << Core::ToIndex(target));
        }
    }
    return bits;
}

[[nodiscard]] bool IsTargetEnabled(const std::uint16_t a_enabledVirtualTargetBits, const Core::Target a_target) {
    if (!Core::IsVirtualTarget(a_target)) {
        return true;
    }

    return (a_enabledVirtualTargetBits & static_cast<std::uint16_t>(1U << Core::ToIndex(a_target))) != 0;
}

void ReadGeneralSettings(CSimpleIniA& a_ini, RawSettings& a_settings) {
    clib_util::ini::get_value(
        a_ini,
        a_settings.debugLoggingEnabled,
        kGeneralSection,
        kDebugLoggingSettingKey,
        "; Enable debug logging.\n; Default: 0",
        clib_util::ini::bool_format::kNumeric
    );
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
    clib_util::ini::get_value(
        a_ini,
        a_settings.unequipAllClearsExtraRings,
        kGeneralSection,
        kUnequipAllClearsExtraRingsSettingKey,
        kUnequipAllClearsExtraRingsSettingComment,
        clib_util::ini::bool_format::kNumeric
    );
}

void ReadActorSettings(CSimpleIniA& a_ini, RawSettings& a_settings) {
    clib_util::ini::get_value(
        a_ini,
        a_settings.npcSupportEnabled,
        kNpcSection,
        kEnableNpcSupportSettingKey,
        kEnableNpcSupportSettingComment,
        clib_util::ini::bool_format::kNumeric
    );
    clib_util::ini::get_value(
        a_ini,
        a_settings.playerAlwaysEquipBondOfMatrimonyOnLeftRingFinger,
        kSpecialRingsSection,
        kPlayerAlwaysEquipBondOfMatrimonyOnLeftRingFingerSettingKey,
        kPlayerAlwaysEquipBondOfMatrimonyOnLeftRingFingerSettingComment,
        clib_util::ini::bool_format::kNumeric
    );
    clib_util::ini::get_value(
        a_ini,
        a_settings.npcAlwaysEquipBondOfMatrimonyOnLeftRingFinger,
        kSpecialRingsSection,
        kNpcAlwaysEquipBondOfMatrimonyOnLeftRingFingerSettingKey,
        kNpcAlwaysEquipBondOfMatrimonyOnLeftRingFingerSettingComment,
        clib_util::ini::bool_format::kNumeric
    );
}

void ReadFingerSettings(CSimpleIniA& a_ini, RawSettings& a_settings) {
    clib_util::ini::get_value(
        a_ini,
        a_settings.alwaysChooseFinger,
        kFingerSelectorSection,
        kAlwaysChooseFingerSettingKey,
        kAlwaysChooseFingerSettingComment,
        clib_util::ini::bool_format::kNumeric
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
}

void ReadVirtualSlotSettings(CSimpleIniA& a_ini, RawSettings& a_settings) {
    for (const auto& setting : kVirtualSlotSettings) {
        clib_util::ini::get_value(
            a_ini,
            a_settings.virtualSlots[Core::ToIndex(setting.target)],
            kVirtualSlotsSection,
            setting.key,
            setting.comment,
            clib_util::ini::bool_format::kNumeric
        );
    }
}

void ReadSettings(CSimpleIniA& a_ini, RawSettings& a_settings) {
    ReadGeneralSettings(a_ini, a_settings);
    ReadActorSettings(a_ini, a_settings);
    ReadFingerSettings(a_ini, a_settings);
    ReadVirtualSlotSettings(a_ini, a_settings);
}

[[nodiscard]] LoadedSettings NormalizeSettings(const RawSettings& a_raw) {
    return LoadedSettings {
        .debugLoggingEnabled = a_raw.debugLoggingEnabled,
        .extraRingMode = ClampExtraRingMode(a_raw.extraRingMode),
        .enchantmentStrengthMode = ClampStrengthMode(a_raw.enchantmentStrengthMode),
        .fixedStrengthPercent = ClampStrengthPercent(a_raw.fixedStrengthPercent),
        .npcSupportEnabled = a_raw.npcSupportEnabled,
        .playerAlwaysEquipBondOfMatrimonyOnLeftRingFinger = a_raw.playerAlwaysEquipBondOfMatrimonyOnLeftRingFinger,
        .npcAlwaysEquipBondOfMatrimonyOnLeftRingFinger = a_raw.npcAlwaysEquipBondOfMatrimonyOnLeftRingFinger,
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
            a_user.SetLongValue(kVirtualSlotsSection, setting.key, loaded ? 1L : 0L);
        }
    }
}

void LogLoadedSettings(const LoadedSettings& a_loaded, const std::filesystem::path& a_userPath) {
    SKSE::log::info(
        "Settings: loaded | path={} | extraRingMode={} | enchantmentStrengthMode={} | fixedStrength={} | npcSupportEnabled={} | playerAlwaysEquipBondOfMatrimonyOnLeftRingFinger={} | npcAlwaysEquipBondOfMatrimonyOnLeftRingFinger={} | unequipAllClearsExtraRings={} | alwaysChooseFinger={} | fingerSelectModifierKey={} | fingerSelectModifierButton={} | enabledVirtualTargets={:04X}",
        a_userPath.string(),
        std::to_underlying(a_loaded.extraRingMode),
        std::to_underlying(a_loaded.enchantmentStrengthMode),
        a_loaded.fixedStrengthPercent,
        a_loaded.npcSupportEnabled,
        a_loaded.playerAlwaysEquipBondOfMatrimonyOnLeftRingFinger,
        a_loaded.npcAlwaysEquipBondOfMatrimonyOnLeftRingFinger,
        a_loaded.unequipAllClearsExtraRings,
        a_loaded.alwaysChooseFinger,
        a_loaded.fingerSelectModifierKey,
        a_loaded.fingerSelectModifierButton,
        a_loaded.enabledVirtualTargetBits
    );
}
}

void Settings::Load() {
    (void)Reload();
}

bool Settings::ReadDebugLoggingEnabled() {
    auto loaded = BMK::Settings::Load(
        {
            .defaults = DefaultSettingsPath(),
            .user = UserSettingsPath(),
        },
        false,
        [](CSimpleIniA& a_defaults, CSimpleIniA& a_user, bool& a_enabled) {
            for (auto* ini : {&a_defaults, &a_user}) {
                clib_util::ini::get_value(
                    *ini,
                    a_enabled,
                    kGeneralSection,
                    kDebugLoggingSettingKey,
                    nullptr,
                    clib_util::ini::bool_format::kNumeric
                );
            }
        },
        BMK::Settings::SaveUserFile::kNo
    );

    return loaded ? loaded->values : false;
}

Settings::ReloadResult Settings::Reload() {
    const auto userPath = UserSettingsPath();
    auto loadedResult = BMK::Settings::Load(
        {
            .defaults = DefaultSettingsPath(),
            .user = userPath,
        },
        LoadedSettings(),
        [](CSimpleIniA& a_defaults, CSimpleIniA& a_user, LoadedSettings& a_loaded) {
            RawSettings raw;
            ReadSettings(a_defaults, raw);
            ReadSettings(a_user, raw);
            a_loaded = NormalizeSettings(raw);
            RepairUserSettings(a_user, raw, a_loaded);
        }
    );
    if (!loadedResult) {
        SKSE::log::error("Settings: {}", loadedResult.error().message);
        return {};
    }
    const auto& loaded = loadedResult->values;
    if (loadedResult->saveFailure) {
        SKSE::log::error("Settings: {}", loadedResult->saveFailure->message);
    }

    BMK::Settings::ApplyLogLevel(loaded.debugLoggingEnabled, SKSE::InitInfo {}.logLevel);

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
    const auto npcSupportChanged = npcSupportEnabled_.exchange(loaded.npcSupportEnabled) != loaded.npcSupportEnabled;
    const auto playerAlwaysEquipBondOfMatrimonyLeftRingFingerChanged
        = playerAlwaysEquipBondOfMatrimonyOnLeftRingFinger_.exchange(
              loaded.playerAlwaysEquipBondOfMatrimonyOnLeftRingFinger
          )
          != loaded.playerAlwaysEquipBondOfMatrimonyOnLeftRingFinger;
    const auto
        npcAlwaysEquipBondOfMatrimonyLeftRingFingerChanged = npcAlwaysEquipBondOfMatrimonyOnLeftRingFinger_.exchange(
                                                                 loaded.npcAlwaysEquipBondOfMatrimonyOnLeftRingFinger
                                                             )
                                                             != loaded.npcAlwaysEquipBondOfMatrimonyOnLeftRingFinger;
    const auto unequipAllClearsExtraRingsChanged = unequipAllClearsExtraRings_.exchange(
                                                       loaded.unequipAllClearsExtraRings
                                                   )
                                                   != loaded.unequipAllClearsExtraRings;
    const auto virtualSlotsChanged = enabledVirtualTargetBits_.exchange(loaded.enabledVirtualTargetBits)
                                     != loaded.enabledVirtualTargetBits;

    LogLoadedSettings(loaded, userPath);

    return ReloadResult {
        .extraRingModeChanged = extraRingModeChanged,
        .enchantmentStrengthChanged = enchantmentStrengthModeChanged || fixedStrengthChanged,
        .fingerSelectionChanged = alwaysChooseFingerChanged || modifierKeyChanged || modifierButtonChanged,
        .playerAlwaysEquipBondOfMatrimonyLeftRingFingerChanged = playerAlwaysEquipBondOfMatrimonyLeftRingFingerChanged,
        .npcAlwaysEquipBondOfMatrimonyLeftRingFingerChanged = npcAlwaysEquipBondOfMatrimonyLeftRingFingerChanged,
        .npcSupportChanged = npcSupportChanged,
        .npcSupportEnabled = loaded.npcSupportEnabled,
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

bool Settings::IsNpcSupportEnabled() const {
    return npcSupportEnabled_.load();
}

bool Settings::ShouldPlayerAlwaysEquipBondOfMatrimonyOnLeftRingFinger() const {
    return playerAlwaysEquipBondOfMatrimonyOnLeftRingFinger_.load();
}

bool Settings::ShouldNpcAlwaysEquipBondOfMatrimonyOnLeftRingFinger() const {
    return npcAlwaysEquipBondOfMatrimonyOnLeftRingFinger_.load();
}

bool Settings::IsActorVirtualRingSupportEnabled(const Core::ActorKey a_actor) const {
    return a_actor && (Core::IsPlayerActorKey(a_actor) || IsNpcSupportEnabled());
}

bool Settings::ShouldUnequipAllClearExtraRings() const {
    return unequipAllClearsExtraRings_.load();
}

bool Settings::IsTargetEnabled(const Core::Target a_target) const {
    return ::IsTargetEnabled(enabledVirtualTargetBits_.load(), a_target);
}

bool Settings::AreTargetsEnabled(const Core::TargetMask& a_targets) const {
    const auto enabledVirtualTargetBits = enabledVirtualTargetBits_.load();
    return std::ranges::all_of(Core::kVirtualTargets, [&](const auto a_target) {
        return !a_targets.Contains(a_target) || ::IsTargetEnabled(enabledVirtualTargetBits, a_target);
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
