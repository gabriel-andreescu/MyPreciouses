#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace UI::Scaleform {
[[nodiscard]] inline bool CanReadMembers(const RE::GFxValue& a_value) {
    return a_value.IsObject() || a_value.IsArray() || a_value.IsDisplayObject();
}

namespace detail {
    [[nodiscard]] inline std::optional<double> ReadNumberMember(const RE::GFxValue& a_object, const char* a_member) {
        if (!CanReadMembers(a_object)) {
            return std::nullopt;
        }

        RE::GFxValue value;
        if (!a_object.GetMember(a_member, std::addressof(value)) || !value.IsNumber()) {
            return std::nullopt;
        }

        const auto number = value.GetNumber();
        if (!std::isfinite(number)) {
            return std::nullopt;
        }

        return number;
    }

    [[nodiscard]] inline bool IsWholeNumber(const double a_value) {
        return std::trunc(a_value) == a_value;
    }
}

[[nodiscard]] inline std::optional<int> ReadIntMember(const RE::GFxValue& a_object, const char* a_member) {
    const auto number = detail::ReadNumberMember(a_object, a_member);
    if (!number || !detail::IsWholeNumber(*number)) {
        return std::nullopt;
    }

    const auto minimum = static_cast<double>(std::numeric_limits<int>::lowest());
    const auto maximum = static_cast<double>(std::numeric_limits<int>::max());
    if (*number < minimum || *number > maximum) {
        return std::nullopt;
    }

    return static_cast<int>(*number);
}

[[nodiscard]] inline std::optional<std::uint32_t> ReadUInt32Member(const RE::GFxValue& a_object, const char* a_member) {
    const auto number = detail::ReadNumberMember(a_object, a_member);
    if (!number || !detail::IsWholeNumber(*number)) {
        return std::nullopt;
    }

    if (*number < 0.0 || *number > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
        return std::nullopt;
    }

    return static_cast<std::uint32_t>(*number);
}

[[nodiscard]] inline std::optional<std::uint16_t> ReadUInt16Member(const RE::GFxValue& a_object, const char* a_member) {
    const auto number = detail::ReadNumberMember(a_object, a_member);
    if (!number || !detail::IsWholeNumber(*number)) {
        return std::nullopt;
    }

    if (*number < 0.0 || *number > static_cast<double>(std::numeric_limits<std::uint16_t>::max())) {
        return std::nullopt;
    }

    return static_cast<std::uint16_t>(*number);
}

[[nodiscard]] inline std::optional<bool> ReadBoolMember(const RE::GFxValue& a_object, const char* a_member) {
    if (!CanReadMembers(a_object)) {
        return std::nullopt;
    }

    RE::GFxValue value;
    if (!a_object.GetMember(a_member, std::addressof(value)) || !value.IsBool()) {
        return std::nullopt;
    }

    return value.GetBool();
}

[[nodiscard]] inline std::optional<std::string> ReadStringMember(const RE::GFxValue& a_object, const char* a_member) {
    if (!CanReadMembers(a_object)) {
        return std::nullopt;
    }

    RE::GFxValue value;
    if (!a_object.GetMember(a_member, std::addressof(value)) || !value.IsString()) {
        return std::nullopt;
    }

    const auto* string = value.GetString();
    return std::string {string ? string : ""};
}
}
