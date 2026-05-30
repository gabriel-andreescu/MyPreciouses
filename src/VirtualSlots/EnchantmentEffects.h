#pragma once

namespace VirtualSlots::EnchantmentEffects {
[[nodiscard]] bool HasMagnitudeEnchantment(const RE::TESObjectARMO& a_source, const RE::ExtraDataList* a_extraList);
void DispelSourceEffects(RE::Actor& a_actor, const RE::TESObjectARMO& a_source);
void ApplyEffectSourceEnchantment(
    RE::Actor& a_actor,
    RE::TESObjectARMO& a_effectSource,
    const RE::ExtraDataList* a_extraList
);
}
