#pragma once

#include <cstdint>
#include <optional>
#include <string>

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

template <class T>
[[nodiscard]] bool WriteField(SKSE::SerializationInterface& a_intfc, const T& a_value) {
    return a_intfc.WriteRecordData(a_value);
}

template <class T>
[[nodiscard]] bool ReadField(SKSE::SerializationInterface& a_intfc, std::uint32_t& a_remaining, T& a_value) {
    if (a_remaining < sizeof(T)) {
        return false;
    }

    const auto read = a_intfc.ReadRecordData(a_value);
    if (read != sizeof(T)) {
        return false;
    }

    a_remaining -= read;
    return true;
}

void DrainRecordData(SKSE::SerializationInterface& a_intfc, std::uint32_t a_remaining);
[[nodiscard]] bool WriteString(SKSE::SerializationInterface& a_intfc, const std::string& a_value);
[[nodiscard]] std::optional<std::string> ReadString(SKSE::SerializationInterface& a_intfc, std::uint32_t& a_remaining);

void Install();
}
