#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "Slots.h"

namespace RuntimeEquipment {
struct AddonModel {
    struct Model {
        std::string path;
        const RE::TESModelTextureSwap* textureSwap {nullptr};
        const RE::TESModelTextureSwap::AlternateTexture* alternateTextures {nullptr};
        std::uint32_t numAlternateTextures {0};
    };

    DisplaySlot channel {DisplaySlot::kRegular};
    const RE::TESObjectARMA* sourceAddon {nullptr};
    std::array<Model, RE::SEXES::kTotal> thirdPerson;
    std::array<Model, RE::SEXES::kTotal> firstPerson;
};

void RequestRefresh();
void Refresh();
void Clear(DisplaySlot a_channel = DisplaySlot::kRegular);
[[nodiscard]] std::optional<RE::FormID> SyncAfterEquip(RE::Actor& a_actor, DisplaySlot a_channel);
void DiscardState(DisplaySlot a_channel);
void DiscardState();
[[nodiscard]] bool IsMatchingCloneWorn(RE::Actor& a_actor, RE::TESForm& a_getEquippedArgument);
[[nodiscard]] bool IsArmor(const RE::TESObjectARMO* a_armor);
[[nodiscard]] bool IsAddon(const RE::TESObjectARMO* a_armor, const RE::TESObjectARMA* a_addon);
[[nodiscard]] float GetEnchantmentScale(RE::Actor& a_actor, const RE::TESObjectARMO* a_source);
[[nodiscard]] std::optional<AddonModel> GetAddonModel(
    const RE::TESObjectARMO* a_armor,
    const RE::TESObjectARMA* a_addon
);
}
