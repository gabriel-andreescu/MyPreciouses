#pragma once

#include <RE/Skyrim.h> // IWYU pragma: keep

namespace VirtualSlots::EnchantmentEffects {
[[nodiscard]] bool HasMagnitudeEnchantment(const RE::EnchantmentItem* a_enchantment);
[[nodiscard]] bool HasMagnitudeEnchantment(const RE::TESObjectARMO& a_source, const RE::ExtraDataList* a_extraList);
[[nodiscard]] bool CanDispelSourceEffects(RE::Actor& a_actor, const RE::TESObjectARMO& a_source);
[[nodiscard]] bool HasSourceEffects(RE::Actor& a_actor, const RE::TESObjectARMO& a_source);
void DispelSourceEffects(RE::Actor& a_actor, const RE::TESObjectARMO& a_source);
void ApplyEffectSourceEnchantment(
    RE::Actor& a_actor,
    RE::TESObjectARMO& a_effectSource,
    RE::EnchantmentItem* a_customEnchantment
);
}
