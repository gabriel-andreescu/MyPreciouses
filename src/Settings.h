#pragma once

#include <REX/REX/Singleton.h>

#include <atomic>
#include <cstdint>

enum class EnchantmentStrengthMode : std::uint32_t {
    kFullStrength = 0,
    kFixedStrength = 1,
    kSplitStrength = 2,
};

enum class ExtraRingMode : std::uint32_t {
    kFunctional = 0,
    kCosmetic = 1,
};

class Settings : public REX::Singleton<Settings> {
public:
    static constexpr std::uint32_t kDefaultEnchantmentStrengthPercent {100};
    static constexpr std::uint32_t kMinimumEnchantmentStrengthPercent {5};
    static constexpr std::uint32_t kDefaultFixedEnchantmentStrengthPercent {50};
    static constexpr std::uint32_t kDefaultFingerSelectModifierKey {42};
    static constexpr std::uint32_t kDefaultFingerSelectModifierButton {274};

    struct ReloadResult {
        bool extraRingModeChanged {false};
        bool enchantmentStrengthChanged {false};
        bool fingerSelectionChanged {false};

        [[nodiscard]] bool Changed() const {
            return extraRingModeChanged || enchantmentStrengthChanged || fingerSelectionChanged;
        }
    };

    void Load();
    [[nodiscard]] ReloadResult Reload();

    [[nodiscard]] ExtraRingMode GetExtraRingMode() const;
    [[nodiscard]] EnchantmentStrengthMode GetEnchantmentStrengthMode() const;
    [[nodiscard]] std::uint32_t GetFixedEnchantmentStrengthPercent() const;
    [[nodiscard]] float GetRingEnchantmentScale(std::uint32_t a_enchantedRingCount) const;
    [[nodiscard]] bool AlwaysChooseFinger() const;
    [[nodiscard]] std::uint32_t GetFingerSelectModifierKey() const;
    [[nodiscard]] std::uint32_t GetFingerSelectModifierButton() const;

private:
    std::atomic<ExtraRingMode> extraRingMode_ {ExtraRingMode::kFunctional};
    std::atomic<EnchantmentStrengthMode> enchantmentStrengthMode_ {EnchantmentStrengthMode::kFullStrength};
    std::atomic<std::uint32_t> fixedEnchantmentStrengthPercent_ {kDefaultFixedEnchantmentStrengthPercent};
    std::atomic_bool alwaysChooseFinger_ {false};
    std::atomic<std::uint32_t> fingerSelectModifierKey_ {kDefaultFingerSelectModifierKey};
    std::atomic<std::uint32_t> fingerSelectModifierButton_ {kDefaultFingerSelectModifierButton};
};
