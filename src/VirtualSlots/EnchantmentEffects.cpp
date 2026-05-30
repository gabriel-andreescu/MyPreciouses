#include "VirtualSlots/EnchantmentEffects.h"

#include "Inventory.h"

#include <algorithm>
#include <vector>

namespace RE {
MagicTarget::IPostCreationModification::~IPostCreationModification() = default;
}

namespace VirtualSlots::EnchantmentEffects {
namespace {
    [[nodiscard]] RE::EnchantmentItem* ResolveEnchantment(
        const RE::TESObjectARMO& a_source,
        const RE::ExtraDataList* a_extraList
    ) {
        if (a_source.formEnchanting) {
            return a_source.formEnchanting;
        }

        if (!a_extraList) {
            return nullptr;
        }

        const auto customData = Inventory::ReadCustomEnchantment(*a_extraList);
        return customData ? customData->enchantment : nullptr;
    }

    [[nodiscard]] bool HasNonZeroMagnitudeEffect(const RE::EnchantmentItem& a_enchantment) {
        return std::ranges::any_of(a_enchantment.effects, [](const auto* a_effect) {
            return a_effect && a_effect->effectItem.magnitude != 0.0F;
        });
    }

    class MarkEnchantmentEffect final : public RE::MagicTarget::IPostCreationModification {
    public:
        void ModifyActiveEffect(RE::ActiveEffect* a_effect) override {
            a_effect->flags.set(RE::ActiveEffect::Flag::kEnchanting);
        }
    };

    void ApplyEffect(
        RE::MagicTarget& a_target,
        RE::Actor& a_actor,
        RE::TESObjectARMO& a_effectSource,
        RE::EnchantmentItem& a_enchantment,
        RE::Effect& a_effect,
        RE::MagicTarget::IPostCreationModification& a_callback
    ) {
        RE::MagicTarget::AddTargetData data {};
        data.caster = &a_actor;
        data.magicItem = &a_enchantment;
        data.effect = &a_effect;
        data.source = &a_effectSource;
        data.magnitude = a_effect.effectItem.magnitude;
        data.power = 1.0F;
        data.castingSource = RE::MagicSystem::CastingSource::kInstant;
        data.areaTarget = false;
        data.dualCasted = false;
        data.postCreationCallback = &a_callback;

        static_cast<void>(a_target.AddTarget(data));
    }
}

bool HasMagnitudeEnchantment(const RE::TESObjectARMO& a_source, const RE::ExtraDataList* a_extraList) {
    auto* enchantment = ResolveEnchantment(a_source, a_extraList);
    return enchantment && HasNonZeroMagnitudeEffect(*enchantment);
}

void DispelSourceEffects(RE::Actor& a_actor, const RE::TESObjectARMO& a_source) {
    auto* magicTarget = a_actor.AsMagicTarget();
    if (!magicTarget) {
        return;
    }

    auto* activeEffects = magicTarget->GetActiveEffectList();
    if (!activeEffects) {
        return;
    }

    std::vector<RE::ActiveEffect*> effects;
    for (auto* activeEffect : *activeEffects) {
        if (!activeEffect || activeEffect->flags.any(RE::ActiveEffect::Flag::kDispelled)) {
            continue;
        }

        if (activeEffect->source == &a_source) {
            effects.push_back(activeEffect);
        }
    }

    for (auto* effect : effects) {
        effect->Dispel(true);
    }
}

void ApplyEffectSourceEnchantment(
    RE::Actor& a_actor,
    RE::TESObjectARMO& a_effectSource,
    const RE::ExtraDataList* a_extraList
) {
    auto* enchantment = ResolveEnchantment(a_effectSource, a_extraList);
    if (!enchantment) {
        return;
    }

    auto* magicTarget = a_actor.AsMagicTarget();
    if (!magicTarget) {
        logger::warn(
            "VirtualSlots: enchantment effects apply skipped | source={:08X} | enchantment={:08X} | reason=noMagicTarget",
            a_effectSource.GetFormID(),
            enchantment->GetFormID()
        );
        return;
    }

    MarkEnchantmentEffect callback;

    for (auto* effect : enchantment->effects) {
        if (!effect) {
            continue;
        }

        ApplyEffect(*magicTarget, a_actor, a_effectSource, *enchantment, *effect, callback);
    }
}
}
