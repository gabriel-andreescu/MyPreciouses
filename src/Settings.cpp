#include "Settings.h"

#include <CLIBUtil/simpleINI.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace {
constexpr auto kModName = "LeftHandRingsSKSE"sv;
constexpr auto kSettingsSection = "General";
constexpr auto kBipedSlotKey = "sBipedSlot";
constexpr auto kEnableBondOfMatrimonyKey = "bEnableBondOfMatrimony";
constexpr auto kBondBipedSlotKey = "sBondBipedSlot";
constexpr auto kEnchantmentPowerPercentKey = "iLeftRingEnchantmentPowerPercent";
constexpr auto kEditorSlotBase = 30u;
constexpr auto kMinimumEnchantmentPowerPercent = 5u;

struct SlotDefinition {
    std::string_view label;
    std::uint32_t slotNumber;
    RE::BIPED_OBJECTS::BIPED_OBJECT object;
};

constexpr std::array kSlotDefinitions {
    SlotDefinition {.label = "Face / Mouth (44)"sv, .slotNumber = 44, .object = RE::BIPED_OBJECTS::kModMouth},
    SlotDefinition {.label = "Neck (45)"sv, .slotNumber = 45, .object = RE::BIPED_OBJECTS::kModNeck},
    SlotDefinition {
        .label = "Chest Outer / Cloak (46)"sv,
        .slotNumber = 46,
        .object = RE::BIPED_OBJECTS::kModChestPrimary
    },
    SlotDefinition {.label = "Back (47)"sv, .slotNumber = 47, .object = RE::BIPED_OBJECTS::kModBack},
    SlotDefinition {.label = "Misc / FX (48)"sv, .slotNumber = 48, .object = RE::BIPED_OBJECTS::kModMisc1},
    SlotDefinition {
        .label = "Pelvis Primary / Skirt (49)"sv,
        .slotNumber = 49,
        .object = RE::BIPED_OBJECTS::kModPelvisPrimary
    },
    SlotDefinition {
        .label = "Pelvis Secondary / Undergarment (52)"sv,
        .slotNumber = 52,
        .object = RE::BIPED_OBJECTS::kModPelvisSecondary
    },
    SlotDefinition {
        .label = "Leg Right / Outer Leg (53)"sv,
        .slotNumber = 53,
        .object = RE::BIPED_OBJECTS::kModLegRight
    },
    SlotDefinition {
        .label = "Leg Left / Secondary Leg (54)"sv,
        .slotNumber = 54,
        .object = RE::BIPED_OBJECTS::kModLegLeft
    },
    SlotDefinition {
        .label = "Face Alternate / Jewelry (55)"sv,
        .slotNumber = 55,
        .object = RE::BIPED_OBJECTS::kModFaceJewelry
    },
    SlotDefinition {
        .label = "Chest Secondary / Torso (56)"sv,
        .slotNumber = 56,
        .object = RE::BIPED_OBJECTS::kModChestSecondary
    },
    SlotDefinition {.label = "Shoulder / Pauldron (57)"sv, .slotNumber = 57, .object = RE::BIPED_OBJECTS::kModShoulder},
    SlotDefinition {
        .label = "Arm Left / Secondary Arm (58)"sv,
        .slotNumber = 58,
        .object = RE::BIPED_OBJECTS::kModArmLeft
    },
    SlotDefinition {
        .label = "Arm Right / Primary Arm (59)"sv,
        .slotNumber = 59,
        .object = RE::BIPED_OBJECTS::kModArmRight
    },
    SlotDefinition {.label = "Misc / Belt / FX (60)"sv, .slotNumber = 60, .object = RE::BIPED_OBJECTS::kModMisc2},
    SlotDefinition {.label = "FX01 (61)"sv, .slotNumber = 61, .object = RE::BIPED_OBJECTS::kFX01},
};

[[nodiscard]] constexpr const SlotDefinition& DefaultSlotDefinition() {
    for (const auto& definition : kSlotDefinitions) {
        if (definition.slotNumber == Settings::kDefaultSlotNumber) {
            return definition;
        }
    }

    return kSlotDefinitions.front();
}

[[nodiscard]] constexpr const SlotDefinition& DefaultBondSlotDefinition() {
    for (const auto& definition : kSlotDefinitions) {
        if (definition.slotNumber == Settings::kDefaultBondSlotNumber) {
            return definition;
        }
    }

    return DefaultSlotDefinition();
}

[[nodiscard]] const SlotDefinition* FindSlotDefinition(const std::uint32_t a_slotNumber) {
    const auto it = std::ranges::find_if(kSlotDefinitions, [a_slotNumber](const SlotDefinition& a_definition) {
        return a_definition.slotNumber == a_slotNumber;
    });
    return it != kSlotDefinitions.end() ? std::addressof(*it) : nullptr;
}

[[nodiscard]] const SlotDefinition* FindSlotDefinitionByObject(const std::uint32_t a_object) {
    const auto it = std::ranges::find_if(kSlotDefinitions, [a_object](const SlotDefinition& a_definition) {
        return std::to_underlying(a_definition.object) == a_object;
    });
    return it != kSlotDefinitions.end() ? std::addressof(*it) : nullptr;
}

[[nodiscard]] bool IsDigit(const char a_value) {
    return a_value >= '0' && a_value <= '9';
}

[[nodiscard]] std::optional<std::uint32_t> ExtractLastNumber(const std::string_view a_value) {
    std::optional<std::uint32_t> result;
    auto index = std::size_t {0};
    while (index < a_value.size()) {
        if (!IsDigit(a_value[index])) {
            ++index;
            continue;
        }

        const auto start = index;
        while (index < a_value.size() && IsDigit(a_value[index])) {
            ++index;
        }

        auto parsed = std::uint32_t {0};
        const auto* first = a_value.data() + start;
        const auto* last = a_value.data() + index;
        const auto [ptr, ec] = std::from_chars(first, last, parsed);
        if (ec == std::errc {} && ptr == last) {
            result = parsed;
        }
    }

    return result;
}

[[nodiscard]] const SlotDefinition* ResolveSlotDefinition(
    const std::string_view a_value,
    const SlotDefinition& a_defaultDefinition,
    const std::string_view a_label
) {
    if (const auto slotNumber = ExtractLastNumber(a_value)) {
        if (const auto* definition = FindSlotDefinition(*slotNumber)) {
            return definition;
        }
    }

    logger::warn(
        "Settings: biped slot fallback | slot={} | value='{}' | fallback={}",
        a_label,
        a_value,
        a_defaultDefinition.label
    );
    return std::addressof(a_defaultDefinition);
}

[[nodiscard]] const SlotDefinition& FirstSlotDefinitionExcept(const SlotDefinition& a_excludedDefinition) {
    const auto it = std::ranges::find_if(kSlotDefinitions, [&a_excludedDefinition](const SlotDefinition& a_entry) {
        return a_entry.slotNumber != a_excludedDefinition.slotNumber;
    });
    return it != kSlotDefinitions.end() ? *it : DefaultBondSlotDefinition();
}

[[nodiscard]] const SlotDefinition* ResolveDistinctBondSlotDefinition(
    const bool a_bondEnabled,
    const SlotDefinition* a_normalDefinition,
    const SlotDefinition* a_bondDefinition
) {
    if (!a_bondEnabled || a_normalDefinition->slotNumber != a_bondDefinition->slotNumber) {
        return a_bondDefinition;
    }

    const auto& defaultBondDefinition = DefaultBondSlotDefinition();
    const auto& fallbackDefinition = defaultBondDefinition.slotNumber != a_normalDefinition->slotNumber
                                         ? defaultBondDefinition
                                         : FirstSlotDefinitionExcept(*a_normalDefinition);
    logger::warn(
        "Settings: Bond slot fallback | normalSlot='{}' | fallback={} | reason=matchesNormalSlot",
        a_normalDefinition->label,
        fallbackDefinition.label
    );
    return std::addressof(fallbackDefinition);
}

[[nodiscard]] std::uint32_t ClampEnchantmentPowerPercent(const int a_value) {
    if (std::cmp_less(a_value, kMinimumEnchantmentPowerPercent)) {
        return kMinimumEnchantmentPowerPercent;
    }

    return std::min(static_cast<std::uint32_t>(a_value), Settings::kDefaultEnchantmentPowerPercent);
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
    (void)Reload();
}

Settings::ReloadResult Settings::Reload() {
    const auto defaultPath = std::filesystem::path {"Data/MCM/Config"} / kModName / "settings.ini";
    const auto userPath = std::filesystem::path {"Data/MCM/Settings"} / std::format("{}.ini", kModName);

    auto rawSlot = std::string {DefaultSlotDefinition().label};
    auto rawBondEnabled = true;
    auto rawBondSlot = std::string {DefaultBondSlotDefinition().label};
    auto rawEnchantmentPowerPercent = static_cast<int>(kDefaultEnchantmentPowerPercent);
    const auto readSettings =
        [&rawSlot, &rawBondEnabled, &rawBondSlot, &rawEnchantmentPowerPercent](CSimpleIniA& a_ini) {
            GetValue(
                a_ini,
                rawSlot,
                kSettingsSection,
                kBipedSlotKey,
                "; Equipment slot used to display regular left-hand rings.\n"
                "; You can enter the full label or just the slot number, such as 58.\n"
                "; Default: Arm Left / Secondary Arm (58)"
            );
            GetValue(
                a_ini,
                rawBondEnabled,
                kSettingsSection,
                kEnableBondOfMatrimonyKey,
                "; Give The Bond of Matrimony its own left-hand ring slot.\n"
                "; Default: true"
            );
            GetValue(
                a_ini,
                rawBondSlot,
                kSettingsSection,
                kBondBipedSlotKey,
                "; Equipment slot used to display The Bond of Matrimony on the left hand.\n"
                "; You can enter the full label or just the slot number, such as 59.\n"
                "; Default: Arm Right / Primary Arm (59)"
            );
            GetValue(
                a_ini,
                rawEnchantmentPowerPercent,
                kSettingsSection,
                kEnchantmentPowerPercentKey,
                "; Left-hand ring enchantment strength while an enchanted right-hand ring is equipped.\n"
                "; Valid range: 5-100.\n"
                "; Default: 100"
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

    const auto* definition = ResolveSlotDefinition(rawSlot, DefaultSlotDefinition(), "normal"sv);
    const auto* bondDefinition = ResolveSlotDefinition(rawBondSlot, DefaultBondSlotDefinition(), "Bond of Matrimony"sv);
    bondDefinition = ResolveDistinctBondSlotDefinition(rawBondEnabled, definition, bondDefinition);
    if (rawBondSlot != bondDefinition->label) {
        rawBondSlot = std::string {bondDefinition->label};
        user.SetValue(kSettingsSection, kBondBipedSlotKey, rawBondSlot.c_str());
    }
    (void)user.SaveFile(userPath.string().c_str());

    const auto enchantmentPowerPercent = ClampEnchantmentPowerPercent(rawEnchantmentPowerPercent);
    const auto previousBipedObject = bipedObject_.exchange(std::to_underlying(definition->object));
    const auto previousBondBipedObject = bondBipedObject_.exchange(std::to_underlying(bondDefinition->object));
    const auto previousBondEnabled = enableBondOfMatrimony_.exchange(rawBondEnabled);
    const auto previousEnchantmentPowerPercent = enchantmentPowerPercent_.exchange(enchantmentPowerPercent);
    logger::info(
        "Settings: loaded | path={} | bipedSlot={} | slotNumber={} | bondEnabled={} | bondBipedSlot={} | bondSlotNumber={} | enchantmentPowerPercent={}",
        userPath.string(),
        definition->label,
        definition->slotNumber,
        rawBondEnabled,
        bondDefinition->label,
        bondDefinition->slotNumber,
        enchantmentPowerPercent
    );

    return ReloadResult {
        .bipedSlotChanged = previousBipedObject != std::to_underlying(definition->object),
        .bondEnabledChanged = previousBondEnabled != rawBondEnabled,
        .bondBipedSlotChanged = previousBondBipedObject != std::to_underlying(bondDefinition->object),
        .enchantmentPowerChanged = previousEnchantmentPowerPercent != enchantmentPowerPercent,
    };
}

bool Settings::IsBondOfMatrimonyEnabled() const {
    return enableBondOfMatrimony_.load();
}

RE::BIPED_OBJECTS::BIPED_OBJECT Settings::GetBipedObject(const DisplaySlot a_channel) const {
    const auto value = a_channel == DisplaySlot::kBond ? bondBipedObject_.load() : bipedObject_.load();
    return static_cast<RE::BIPED_OBJECTS::BIPED_OBJECT>(value);
}

std::uint32_t Settings::GetSlotNumber(const DisplaySlot a_channel) const {
    const auto value = a_channel == DisplaySlot::kBond ? bondBipedObject_.load() : bipedObject_.load();
    return kEditorSlotBase + value;
}

std::string_view Settings::GetSlotLabel(const DisplaySlot a_channel) const {
    const auto value = a_channel == DisplaySlot::kBond ? bondBipedObject_.load() : bipedObject_.load();
    if (const auto* definition = FindSlotDefinitionByObject(value)) {
        return definition->label;
    }

    return a_channel == DisplaySlot::kBond ? DefaultBondSlotDefinition().label : DefaultSlotDefinition().label;
}

std::uint32_t Settings::GetEnchantmentPowerPercent() const {
    return enchantmentPowerPercent_.load();
}
