#pragma once

#include <REX/REX/Singleton.h>

#include <atomic>
#include <cstdint>

enum class EnchantmentStrengthMode : std::uint32_t {
    kFullStrength = 0,
    kFixedStrength = 1,
    kSplitStrength = 2,
};

class Settings : public REX::Singleton<Settings> {
public:
    static constexpr std::uint32_t kDefaultEnchantmentStrengthPercent {100};
    static constexpr std::uint32_t kMinimumEnchantmentStrengthPercent {5};
    static constexpr std::uint32_t kDefaultFixedEnchantmentStrengthPercent {50};

    struct ReloadResult {
        bool enchantmentStrengthChanged {false};
        bool fingerSelectionChanged {false};

        [[nodiscard]] bool Changed() const {
            return enchantmentStrengthChanged || fingerSelectionChanged;
        }
    };

    void Load();
    [[nodiscard]] ReloadResult Reload();

    [[nodiscard]] EnchantmentStrengthMode GetEnchantmentStrengthMode() const;
    [[nodiscard]] std::uint32_t GetFixedEnchantmentStrengthPercent() const;
    [[nodiscard]] float GetRingEnchantmentScale(std::uint32_t a_enchantedRingCount) const;
    [[nodiscard]] bool AlwaysChooseFinger() const;

private:
    std::atomic<EnchantmentStrengthMode> enchantmentStrengthMode_ {EnchantmentStrengthMode::kFullStrength};
    std::atomic<std::uint32_t> fixedEnchantmentStrengthPercent_ {kDefaultFixedEnchantmentStrengthPercent};
    std::atomic_bool alwaysChooseFinger_ {false};
};
