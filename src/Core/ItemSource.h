#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace Core {
enum class ItemSourceKind : std::uint32_t {
    kNone = 0,
    kFormOnly = 1,
    kCustomEnchantment = 2,
};

struct CustomEnchantmentSignature {
    RE::FormID enchantmentFormID {0};
    std::uint16_t charge {0};
    bool removeOnUnequip {false};
    std::string playerDisplayName;

    [[nodiscard]] bool IsValid() const {
        return enchantmentFormID != 0;
    }

    [[nodiscard]] bool operator==(const CustomEnchantmentSignature&) const = default;
};

struct ExtraUniqueIDKey {
    RE::FormID baseID {0};
    std::uint16_t uniqueID {0};

    [[nodiscard]] bool IsValid() const {
        return baseID != 0 && uniqueID != 0;
    }

    [[nodiscard]] bool operator==(const ExtraUniqueIDKey&) const = default;
};

struct ItemSource {
    ItemSourceKind kind {ItemSourceKind::kNone};
    RE::FormID sourceFormID {0};
    CustomEnchantmentSignature customEnchantment;
    std::optional<ExtraUniqueIDKey> extraUniqueID;

    [[nodiscard]] bool IsFormOnly() const {
        return kind == ItemSourceKind::kFormOnly && sourceFormID != 0;
    }

    [[nodiscard]] bool IsCustomEnchantment() const {
        return kind == ItemSourceKind::kCustomEnchantment && sourceFormID != 0 && customEnchantment.IsValid();
    }

    [[nodiscard]] bool IsAssigned() const {
        return IsFormOnly() || IsCustomEnchantment();
    }

    [[nodiscard]] bool MatchesForm(RE::FormID a_sourceFormID) const {
        return IsFormOnly() && sourceFormID == a_sourceFormID;
    }

    [[nodiscard]] bool MatchesCustomEnchantment(
        RE::FormID a_sourceFormID,
        const CustomEnchantmentSignature& a_signature,
        const std::optional<ExtraUniqueIDKey>& a_uniqueID = std::nullopt
    ) const {
        if (!IsCustomEnchantment() || sourceFormID != a_sourceFormID || customEnchantment != a_signature) {
            return false;
        }

        return !extraUniqueID || (a_uniqueID && *extraUniqueID == *a_uniqueID);
    }

    [[nodiscard]] bool MatchesSourceFormID(RE::FormID a_sourceFormID) const {
        return IsAssigned() && sourceFormID == a_sourceFormID;
    }

    [[nodiscard]] bool Matches(const ItemSource& a_source) const {
        if (!a_source.IsAssigned()) {
            return false;
        }

        if (a_source.IsCustomEnchantment()) {
            return MatchesCustomEnchantment(a_source.sourceFormID, a_source.customEnchantment, a_source.extraUniqueID);
        }

        if (a_source.IsFormOnly()) {
            return MatchesForm(a_source.sourceFormID);
        }

        return false;
    }

    [[nodiscard]] bool operator==(const ItemSource&) const = default;
};
}
