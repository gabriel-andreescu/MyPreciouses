#include "VirtualSlots/EnchantmentEffects.h"

#include <RE/Skyrim.h> // IWYU pragma: keep
#include <SKSE/SKSE.h> // IWYU pragma: keep

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

        const auto* custom = a_extraList->GetByType<RE::ExtraEnchantment>();
        return custom ? custom->enchantment : nullptr;
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

bool HasMagnitudeEnchantment(const RE::EnchantmentItem* a_enchantment) {
    return a_enchantment != nullptr && std::ranges::any_of(a_enchantment->effects, [](const auto* a_effect) {
        return a_effect != nullptr && a_effect->effectItem.magnitude != 0.0F;
    });
}

bool HasMagnitudeEnchantment(const RE::TESObjectARMO& a_source, const RE::ExtraDataList* a_extraList) {
    return HasMagnitudeEnchantment(ResolveEnchantment(a_source, a_extraList));
}

bool CanDispelSourceEffects(RE::Actor& a_actor, const RE::TESObjectARMO& a_source) {
    auto* effects = a_actor.AsMagicTarget()->GetActiveEffectList();
    return effects == nullptr || std::ranges::none_of(*effects, [&a_source](auto* a_effect) {
        return a_effect
               != nullptr
               && a_effect->source
               == &a_source
               && !a_effect->flags.any(RE::ActiveEffect::Flag::kDispelled)
               && !a_effect->CanFinish();
    });
}

bool HasSourceEffects(RE::Actor& a_actor, const RE::TESObjectARMO& a_source) {
    auto* effects = a_actor.AsMagicTarget()->GetActiveEffectList();
    return effects != nullptr && std::ranges::any_of(*effects, [&a_source](const auto* a_effect) {
        return a_effect
               != nullptr
               && a_effect->source
               == &a_source
               && !a_effect->flags.any(RE::ActiveEffect::Flag::kDispelled);
    });
}

void DispelSourceEffects(RE::Actor& a_actor, const RE::TESObjectARMO& a_source) {
    auto* activeEffects = a_actor.AsMagicTarget()->GetActiveEffectList(); // NOLINT(misc-const-correctness)
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
    RE::EnchantmentItem* a_customEnchantment
) {
    auto* enchantment = a_effectSource.formEnchanting ? a_effectSource.formEnchanting : a_customEnchantment;
    if (!enchantment) {
        return;
    }

    auto* magicTarget = a_actor.AsMagicTarget();
    MarkEnchantmentEffect callback;

    for (auto* effect : enchantment->effects) {
        if (!effect) {
            continue;
        }

        ApplyEffect(*magicTarget, a_actor, a_effectSource, *enchantment, *effect, callback);
    }
}
}
