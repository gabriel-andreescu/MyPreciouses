#pragma once

#include <array>
#include <cstdint>
#include <string_view>

enum class DisplaySlot : std::uint32_t {
    kRegular = 0,
    kBond = 1,
};

inline constexpr std::array kDisplaySlots {
    DisplaySlot::kRegular,
    DisplaySlot::kBond,
};

[[nodiscard]] constexpr std::uint32_t ToIndex(const DisplaySlot a_slot) {
    return static_cast<std::uint32_t>(a_slot);
}

[[nodiscard]] constexpr std::string_view DisplaySlotLabel(const DisplaySlot a_slot) {
    switch (a_slot) {
        case DisplaySlot::kRegular: return "normal";
        case DisplaySlot::kBond:    return "bond";
    }

    return "unknown";
}

namespace Slots {
namespace detail {
    constexpr std::uint32_t kDismemberPartBase = 30;
}

[[nodiscard]] constexpr auto ToArmorSlot(const RE::BIPED_OBJECTS::BIPED_OBJECT a_object) {
    return static_cast<RE::BGSBipedObjectForm::BipedObjectSlot>(1u << static_cast<std::uint32_t>(a_object));
}

[[nodiscard]] constexpr auto ToDismemberPartID(const RE::BIPED_OBJECTS::BIPED_OBJECT a_object) {
    return static_cast<std::uint16_t>(detail::kDismemberPartBase + static_cast<std::uint32_t>(a_object));
}

[[nodiscard]] RE::BIPED_OBJECTS::BIPED_OBJECT GetBipedObject(DisplaySlot a_channel = DisplaySlot::kRegular);
[[nodiscard]] RE::BGSBipedObjectForm::BipedObjectSlot GetArmorSlot(DisplaySlot a_channel = DisplaySlot::kRegular);
[[nodiscard]] RE::BGSBipedObjectForm::FirstPersonFlag GetFirstPersonFlag(DisplaySlot a_channel = DisplaySlot::kRegular);
[[nodiscard]] std::uint16_t GetDismemberPartID(DisplaySlot a_channel = DisplaySlot::kRegular);
}
