#pragma once

#include <cstdint>

namespace Serialization {
[[nodiscard]] constexpr std::uint32_t MakeRecordType(
    const char a_first,
    const char a_second,
    const char a_third,
    const char a_fourth
) {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(a_first)) << 24)
           | (static_cast<std::uint32_t>(static_cast<unsigned char>(a_second)) << 16)
           | (static_cast<std::uint32_t>(static_cast<unsigned char>(a_third)) << 8)
           | static_cast<std::uint32_t>(static_cast<unsigned char>(a_fourth));
}

struct RecordInfo {
    std::uint32_t type;
    std::uint32_t version;
    std::uint32_t length;
};

void Install();
}
