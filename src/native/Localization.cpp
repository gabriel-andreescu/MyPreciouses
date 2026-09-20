#include "Localization.h"
#include "Core/Target.h"

#include <RE/Skyrim.h> // IWYU pragma: keep
#include <SKSE/SKSE.h> // IWYU pragma: keep

#include <RE/B/BSResourceNiBinaryStream.h>
#include <RE/I/INISettingCollection.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace {
using TranslationMap = std::unordered_map<std::string, std::string>;

constexpr auto kFingerThumbKey = "$MYPRECIOUSES_Finger_Thumb";
constexpr auto kFingerIndexKey = "$MYPRECIOUSES_Finger_Index";
constexpr auto kFingerMiddleKey = "$MYPRECIOUSES_Finger_Middle";
constexpr auto kFingerRingKey = "$MYPRECIOUSES_Finger_Ring";
constexpr auto kFingerPinkyKey = "$MYPRECIOUSES_Finger_Pinky";

[[nodiscard]] TranslationMap& Translations() {
    static TranslationMap translations;
    return translations;
}

[[nodiscard]] std::string GetConfiguredLanguage() {
    auto* ini = RE::INISettingCollection::GetSingleton();
    const auto* setting = ini ? ini->GetSetting("sLanguage:General") : nullptr;
    if (setting && setting->GetType() == RE::Setting::Type::kString && setting->data.s) {
        std::string language {setting->data.s};
        std::ranges::transform(language, language.begin(), [](const unsigned char a_ch) {
            return static_cast<char>(std::toupper(a_ch));
        });
        return language;
    }

    return "ENGLISH";
}

[[nodiscard]] std::string ToUtf8(const std::wstring_view a_text) {
    return SKSE::stl::utf16_to_utf8(a_text).value_or(std::string {""});
}

template <std::size_t Size>
[[nodiscard]] bool ReadUtf16Line(
    const RE::BSResource::Stream& a_stream,
    std::array<wchar_t, Size>& a_dst,
    std::uint32_t& a_len
) {
    auto readAny = false;
    auto length = std::size_t {0};
    for (; length + 1 < a_dst.size(); ++length) {
        wchar_t data = 0;
        std::uint64_t read = 0;
        a_stream.DoRead(std::addressof(data), sizeof(data), read);
        if (read != sizeof(data)) {
            break;
        }

        readAny = true;
        if (data == L'\n') {
            break;
        }

        a_dst[length] = data;
    }

    a_dst[length] = 0;
    a_len = static_cast<std::uint32_t>(length);
    return readAny;
}

void LoadTranslationFile(const std::string_view a_name, const std::string_view a_language) {
    const auto path = std::format("Interface\\Translations\\{}_{}.txt", a_name, a_language);
    RE::BSResourceNiBinaryStream const fileStream {path};
    if (!fileStream.good()) {
        return;
    }

    std::uint16_t bom = 0;
    std::uint64_t read = 0;
    fileStream.stream->DoRead(std::addressof(bom), sizeof(bom), read);
    if (read != sizeof(bom) || bom != 0xFEFF) {
        SKSE::log::warn("Localization: load failed | path={} | reason=invalidEncoding", path);
        return;
    }

    std::size_t count = 0;
    while (true) {
        std::array<wchar_t, 512> line {};
        std::uint32_t len = 0;
        if (!ReadUtf16Line(*fileStream.stream, line, len)) {
            SKSE::log::info("Localization: loaded | count={} | path={}", count, path);
            return;
        }

        if (len == 0) {
            continue;
        }

        if (line[len - 1] == L'\r') {
            --len;
        }

        if (len == 0) {
            continue;
        }

        if (len < 4 || line[0] != L'$') {
            continue;
        }

        std::uint32_t delimiter = 0;
        for (std::uint32_t i = 0; i < len; ++i) {
            if (line[i] == L'\t') {
                delimiter = i;
            }
        }

        if (delimiter < 2 || delimiter + 1 >= len) {
            continue;
        }

        const auto key = ToUtf8(std::wstring_view {line.data(), delimiter});
        const auto value = ToUtf8(std::wstring_view {std::addressof(line[delimiter + 1]), len - delimiter - 1});
        Translations().insert_or_assign(key, value);
        ++count;
    }
}
}

namespace Localization {
void Load(const std::string_view a_name) {
    Translations().clear();

    const auto language = GetConfiguredLanguage();
    LoadTranslationFile(a_name, "ENGLISH");
    if (language != "ENGLISH") {
        LoadTranslationFile(a_name, language);
    }
}

std::string Translate(const std::string_view a_key, const std::string_view a_fallback) {
    auto& translations = Translations();
    if (const auto translation = translations.find(std::string {a_key}); translation != translations.end()) {
        return translation->second;
    }

    return std::string {a_fallback};
}

std::string TranslateFingerLabel(const Core::Finger a_finger) {
    switch (a_finger) {
        case Core::Finger::kThumb:  return Translate(kFingerThumbKey, "Thumb");
        case Core::Finger::kIndex:  return Translate(kFingerIndexKey, "Index");
        case Core::Finger::kMiddle: return Translate(kFingerMiddleKey, "Middle");
        case Core::Finger::kRing:   return Translate(kFingerRingKey, "Ring");
        case Core::Finger::kPinky:  return Translate(kFingerPinkyKey, "Pinky");
    }

    return "Unknown";
}
}
