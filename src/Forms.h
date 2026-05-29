#pragma once

#include <string_view>

namespace Forms {
inline constexpr std::string_view kSkyrimESM {"Skyrim.esm"};
inline constexpr std::string_view kDragonbornESM {"Dragonborn.esm"};

inline constexpr RE::FormID kRightHandEquipSlotFormID {0x00013F42};
inline constexpr RE::FormID kLeftHandEquipSlotFormID {0x00013F43};
}
