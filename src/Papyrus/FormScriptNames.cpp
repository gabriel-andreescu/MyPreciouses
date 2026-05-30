#include "Papyrus/FormScriptNames.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace Papyrus::FormScriptNames {
namespace {
    constexpr std::uint8_t kObjectProperty = 1;
    constexpr std::uint8_t kStringProperty = 2;
    constexpr std::uint8_t kIntegerProperty = 3;
    constexpr std::uint8_t kFloatProperty = 4;
    constexpr std::uint8_t kBooleanProperty = 5;
    constexpr std::uint8_t kFirstArrayProperty = 11;
    constexpr std::uint8_t kLastArrayProperty = 15;
    constexpr std::uint8_t kArrayElementOffset = 10;
    constexpr std::uint8_t kDeletedScriptStatus = 3;
    constexpr std::uint16_t kScriptStatusVersion = 4;
    constexpr std::size_t kObjectPropertyValueSize = 8;
    constexpr std::size_t kNumberPropertyValueSize = 4;
    constexpr std::size_t kBooleanPropertyValueSize = 1;

    class VmadReader {
    public:
        explicit VmadReader(const std::vector<std::uint8_t>& a_data)
            : data_(a_data) {}

        template <class T>
        [[nodiscard]] bool Read(T& a_out) {
            if (offset_ + sizeof(T) > data_.size()) {
                return false;
            }

            std::memcpy(std::addressof(a_out), data_.data() + offset_, sizeof(T));
            offset_ += sizeof(T);
            return true;
        }

        [[nodiscard]] std::optional<std::string> ReadString16() {
            std::uint16_t length = 0;
            if (!Read(length) || offset_ + length > data_.size()) {
                return std::nullopt;
            }

            std::string value {reinterpret_cast<const char*>(data_.data() + offset_), length};
            offset_ += length;
            if (!value.empty() && value.back() == '\0') {
                value.pop_back();
            }

            return value;
        }

        [[nodiscard]] bool Skip(const std::size_t a_bytes) {
            if (offset_ + a_bytes > data_.size()) {
                return false;
            }

            offset_ += a_bytes;
            return true;
        }

    private:
        const std::vector<std::uint8_t>& data_;
        std::size_t offset_ {0};
    };

    struct ScopedTESFile {
        explicit ScopedTESFile(RE::TESFile* a_file)
            : file(a_file) {}

        ScopedTESFile(const ScopedTESFile&) = delete;
        ScopedTESFile(ScopedTESFile&&) = delete;
        ScopedTESFile& operator=(const ScopedTESFile&) = delete;
        ScopedTESFile& operator=(ScopedTESFile&&) = delete;

        ~ScopedTESFile() {
            if (file) {
                file->CloseTES(true);
            }
        }

        RE::TESFile* file {nullptr};
    };

    [[nodiscard]] constexpr std::uint32_t MakeSubrecordType(
        const char a_first,
        const char a_second,
        const char a_third,
        const char a_fourth
    ) {
        return static_cast<std::uint8_t>(a_first)
               | (static_cast<std::uint8_t>(a_second) << 8)
               | (static_cast<std::uint8_t>(a_third) << 16)
               | (static_cast<std::uint8_t>(a_fourth) << 24);
    }

    [[nodiscard]] bool ContainsScriptName(
        const std::vector<RE::BSFixedString>& a_scriptNames,
        const RE::BSFixedString& a_scriptName
    ) {
        return std::ranges::find(a_scriptNames, a_scriptName) != a_scriptNames.end();
    }

    [[nodiscard]] bool IsArrayProperty(const std::uint8_t a_propertyType) {
        return a_propertyType >= kFirstArrayProperty && a_propertyType <= kLastArrayProperty;
    }

    [[nodiscard]] bool SkipPropertyValue(VmadReader& a_reader, std::uint8_t a_propertyType);

    [[nodiscard]] bool SkipSinglePropertyValue(VmadReader& a_reader, const std::uint8_t a_propertyType) {
        switch (a_propertyType) {
            case kObjectProperty:  return a_reader.Skip(kObjectPropertyValueSize);
            case kStringProperty:  return a_reader.ReadString16().has_value();
            case kIntegerProperty:
            case kFloatProperty:   return a_reader.Skip(kNumberPropertyValueSize);
            case kBooleanProperty: return a_reader.Skip(kBooleanPropertyValueSize);
            default:
                if (IsArrayProperty(a_propertyType)) {
                    return SkipPropertyValue(a_reader, a_propertyType);
                }
                return false;
        }
    }

    [[nodiscard]] bool SkipPropertyValue(VmadReader& a_reader, const std::uint8_t a_propertyType) {
        if (!IsArrayProperty(a_propertyType)) {
            return SkipSinglePropertyValue(a_reader, a_propertyType);
        }

        std::uint32_t itemCount = 0;
        if (!a_reader.Read(itemCount)) {
            return false;
        }

        const auto elementType = static_cast<std::uint8_t>(a_propertyType - kArrayElementOffset);
        for (std::uint32_t index = 0; index < itemCount; ++index) {
            if (!SkipSinglePropertyValue(a_reader, elementType)) {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] std::optional<std::vector<RE::BSFixedString>> ParseRecordScriptNames(
        const std::vector<std::uint8_t>& a_data,
        const RE::TESForm& a_form
    ) {
        std::vector<RE::BSFixedString> scriptNames;
        VmadReader reader {a_data};

        std::uint16_t version = 0;
        std::uint16_t objectFormat = 0;
        std::uint16_t scriptCount = 0;
        if (!reader.Read(version) || !reader.Read(objectFormat) || !reader.Read(scriptCount)) {
            logger::warn("Papyrus: form script scan failed | form={:08X} | reason=header", a_form.GetFormID());
            return std::nullopt;
        }

        for (std::uint16_t scriptIndex = 0; scriptIndex < scriptCount; ++scriptIndex) {
            const auto scriptName = reader.ReadString16();
            if (!scriptName) {
                logger::warn(
                    "Papyrus: form script scan failed | form={:08X} | scriptIndex={} | objectFormat={} | reason=scriptName",
                    a_form.GetFormID(),
                    scriptIndex,
                    objectFormat
                );
                return std::nullopt;
            }

            std::uint8_t scriptStatus = 0;
            if (version >= kScriptStatusVersion && !reader.Read(scriptStatus)) {
                logger::warn(
                    "Papyrus: form script scan failed | form={:08X} | script={} | objectFormat={} | reason=scriptStatus",
                    a_form.GetFormID(),
                    scriptName->c_str(),
                    objectFormat
                );
                return std::nullopt;
            }

            std::uint16_t propertyCount = 0;
            if (!reader.Read(propertyCount)) {
                logger::warn(
                    "Papyrus: form script scan failed | form={:08X} | script={} | objectFormat={} | reason=propertyCount",
                    a_form.GetFormID(),
                    scriptName->c_str(),
                    objectFormat
                );
                return std::nullopt;
            }

            if (scriptStatus != kDeletedScriptStatus) {
                const RE::BSFixedString fixedName {*scriptName};
                if (!fixedName.empty() && !ContainsScriptName(scriptNames, fixedName)) {
                    scriptNames.push_back(fixedName);
                }
            }

            for (std::uint16_t propertyIndex = 0; propertyIndex < propertyCount; ++propertyIndex) {
                const auto propertyName = reader.ReadString16();
                std::uint8_t propertyType = 0;
                std::uint8_t propertyStatus = 0;
                if (!propertyName
                    || !reader.Read(propertyType)
                    || (version >= kScriptStatusVersion && !reader.Read(propertyStatus))
                    || !SkipPropertyValue(reader, propertyType)) {
                    logger::warn(
                        "Papyrus: form script scan failed | form={:08X} | script={} | propertyIndex={} | objectFormat={} | reason=property",
                        a_form.GetFormID(),
                        scriptName->c_str(),
                        propertyIndex,
                        objectFormat
                    );
                    return std::nullopt;
                }
            }
        }

        return scriptNames;
    }
}

std::vector<RE::BSFixedString> GetRecordScriptNames(const RE::TESForm& a_form) {
    constexpr auto kVMAD = MakeSubrecordType('V', 'M', 'A', 'D');
    std::vector<RE::BSFixedString> scriptNames;

    auto* sourceFile = a_form.GetFile();
    if (!sourceFile) {
        return scriptNames;
    }

    ScopedTESFile file {sourceFile->Duplicate()};
    if (!file.file) {
        logger::warn(
            "Papyrus: form scripts unavailable | form={:08X} | file={} | reason=duplicateFailed",
            a_form.GetFormID(),
            sourceFile->GetFilename()
        );
        return scriptNames;
    }

    auto* mutableForm = const_cast<RE::TESForm*>(std::addressof(a_form));
    if (!file.file->SeekForm(mutableForm) || !file.file->SeekNextSubrecordType(kVMAD)) {
        return scriptNames;
    }

    const auto size = file.file->GetCurrentSubRecordSize();
    if (size == 0) {
        return scriptNames;
    }

    std::vector<std::uint8_t> data(size);
    if (!file.file->ReadData(data.data(), size)) {
        logger::warn(
            "Papyrus: form scripts unavailable | form={:08X} | file={} | reason=readFailed",
            a_form.GetFormID(),
            sourceFile->GetFilename()
        );
        return scriptNames;
    }

    return ParseRecordScriptNames(data, a_form).value_or(std::vector<RE::BSFixedString> {});
}
}
