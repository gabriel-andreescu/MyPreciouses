#pragma once

#include "Slots.h"

#include <REX/REX/Singleton.h>

#include <atomic>
#include <cstdint>
#include <string_view>

class Settings : public REX::Singleton<Settings> {
public:
    static constexpr std::uint32_t kDefaultSlotNumber {58};
    static constexpr std::uint32_t kDefaultBondSlotNumber {59};
    static constexpr std::uint32_t kDefaultEnchantmentPowerPercent {100};

    struct ReloadResult {
        bool bipedSlotChanged {false};
        bool bondEnabledChanged {false};
        bool bondBipedSlotChanged {false};
        bool enchantmentPowerChanged {false};

        [[nodiscard]] bool Changed() const {
            return bipedSlotChanged || bondEnabledChanged || bondBipedSlotChanged || enchantmentPowerChanged;
        }

        [[nodiscard]] bool DisplaySlotsChanged() const {
            return bipedSlotChanged || bondEnabledChanged || bondBipedSlotChanged;
        }
    };

    void Load();
    [[nodiscard]] ReloadResult Reload();

    [[nodiscard]] bool IsBondOfMatrimonyEnabled() const;
    [[nodiscard]] RE::BIPED_OBJECTS::BIPED_OBJECT GetBipedObject(DisplaySlot a_channel = DisplaySlot::kRegular) const;
    [[nodiscard]] std::uint32_t GetSlotNumber(DisplaySlot a_channel = DisplaySlot::kRegular) const;
    [[nodiscard]] std::string_view GetSlotLabel(DisplaySlot a_channel = DisplaySlot::kRegular) const;
    [[nodiscard]] std::uint32_t GetEnchantmentPowerPercent() const;

private:
    std::atomic<std::uint32_t> bipedObject_ {kDefaultSlotNumber - 30};
    std::atomic<std::uint32_t> bondBipedObject_ {kDefaultBondSlotNumber - 30};
    std::atomic_bool enableBondOfMatrimony_ {true};
    std::atomic<std::uint32_t> enchantmentPowerPercent_ {kDefaultEnchantmentPowerPercent};
};
