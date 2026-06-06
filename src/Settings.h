#pragma once

#include "Core/ActorKey.h"
#include "Core/TargetMask.h"

#include <REX/REX/Singleton.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <utility>

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
    static constexpr std::uint32_t kMaximumEnchantmentStrengthPercent {100};
    static constexpr std::uint32_t kMinimumEnchantmentStrengthPercent {5};
    static constexpr std::uint32_t kDefaultFixedEnchantmentStrengthPercent {50};
    static constexpr std::uint32_t kDefaultFingerSelectModifierKey {42};
    static constexpr std::uint32_t kVanillaInventoryHintModifierButton {275};
    static constexpr std::uint32_t kDefaultFingerSelectModifierButton {kVanillaInventoryHintModifierButton};
    static constexpr std::uint16_t kDefaultEnabledVirtualTargetBits {[] {
        auto bits = std::uint16_t {0};
        for (const auto target : Core::kVirtualTargets) {
            bits |= static_cast<std::uint16_t>(1u << Core::ToIndex(target));
        }
        return bits;
    }()};

    struct ReloadResult {
        bool extraRingModeChanged {false};
        bool enchantmentStrengthChanged {false};
        bool fingerSelectionChanged {false};
        bool npcSupportChanged {false};
        bool npcSupportEnabled {true};
        bool unequipAllClearsExtraRingsChanged {false};
        bool unequipAllClearsExtraRingsEnabled {true};
        bool virtualSlotsChanged {false};

        [[nodiscard]] bool Changed() const {
            return extraRingModeChanged
                   || enchantmentStrengthChanged
                   || fingerSelectionChanged
                   || npcSupportChanged
                   || unequipAllClearsExtraRingsChanged
                   || virtualSlotsChanged;
        }
    };

    void Load();
    [[nodiscard]] static bool ReadDebugLoggingEnabled();
    [[nodiscard]] ReloadResult Reload();

    [[nodiscard]] ExtraRingMode GetExtraRingMode() const;
    [[nodiscard]] float GetRingEnchantmentScale(std::uint32_t a_enchantedRingCount) const;
    [[nodiscard]] bool AlwaysChooseFinger() const;
    [[nodiscard]] std::uint32_t GetFingerSelectModifierKey() const;
    [[nodiscard]] std::uint32_t GetFingerSelectModifierButton() const;
    [[nodiscard]] bool IsNpcSupportEnabled() const;
    [[nodiscard]] bool IsActorVirtualRingSupportEnabled(Core::ActorKey a_actor) const;
    [[nodiscard]] bool ShouldUnequipAllClearExtraRings() const;
    [[nodiscard]] bool IsTargetEnabled(Core::Target a_target) const;
    [[nodiscard]] bool AreTargetsEnabled(const Core::TargetMask& a_targets) const;
    [[nodiscard]] std::optional<Core::Target> GetDefaultLeftTarget() const;

private:
    std::atomic<ExtraRingMode> extraRingMode_ {ExtraRingMode::kFunctional};
    std::atomic<EnchantmentStrengthMode> enchantmentStrengthMode_ {EnchantmentStrengthMode::kFullStrength};
    std::atomic<std::uint32_t> fixedEnchantmentStrengthPercent_ {kDefaultFixedEnchantmentStrengthPercent};
    std::atomic_bool alwaysChooseFinger_ {false};
    std::atomic<std::uint32_t> fingerSelectModifierKey_ {kDefaultFingerSelectModifierKey};
    std::atomic<std::uint32_t> fingerSelectModifierButton_ {kDefaultFingerSelectModifierButton};
    std::atomic_bool npcSupportEnabled_ {true};
    std::atomic_bool unequipAllClearsExtraRings_ {true};
    std::atomic<std::uint16_t> enabledVirtualTargetBits_ {kDefaultEnabledVirtualTargetBits};
};
