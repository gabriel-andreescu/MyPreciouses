#pragma once

#include "EventBindings.h"
#include "RingFootprints.h"
#include "RingSounds.h"
#include "RingTargets.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace VirtualRings {
struct RefreshOptions {
    std::optional<RingTarget> soundTarget;
    RingSounds::Event sound {RingSounds::Event::kNone};
};

struct SourceAddonVisual {
    struct ModelVariant {
        std::string path;
        const RE::TESModelTextureSwap* textureSwap {nullptr};
        const RE::TESModelTextureSwap::AlternateTexture* alternateTextures {nullptr};
        std::uint32_t numAlternateTextures {0};
    };

    RingTarget target {kDefaultLeftEquipTarget};
    const RE::TESObjectARMA* sourceAddon {nullptr};
    std::array<ModelVariant, RE::SEXES::kTotal> thirdPerson;
    std::array<ModelVariant, RE::SEXES::kTotal> firstPerson;
};

struct VisualEntry {
    RingTarget target {kDefaultLeftEquipTarget};
    RingFootprints::RingFootprint footprint;
    RE::FormID sourceFormID {0};
    std::vector<SourceAddonVisual> addonVisuals;
};

void RequestRefresh(RefreshOptions a_options = {});
void Refresh();
void Clear(RingTarget a_target, RingSounds::Event a_sound = RingSounds::Event::kNone);
void Revert();

[[nodiscard]] bool MatchesGetEquippedCondition(RE::Actor& a_actor, RE::TESForm& a_getEquippedArgument);
[[nodiscard]] bool IsEffectSource(const RE::TESObjectARMO* a_armor);
[[nodiscard]] bool IsEnchantedVirtualRingEffectSource(const RE::TESObjectARMO* a_armor);
[[nodiscard]] std::uint32_t CountEnchantedVirtualRings();
[[nodiscard]] std::vector<VisualEntry> GetVisualEntries();
[[nodiscard]] std::vector<EventBindings::ScriptBindingSource> GetEventBindingSources();
}
