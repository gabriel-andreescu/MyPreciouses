#include "VirtualSlots/EffectSources.h"

#include <RE/Skyrim.h> // IWYU pragma: keep
#include <SKSE/SKSE.h> // IWYU pragma: keep

#include "Core/ItemSource.h"
#include "Serialization.h"
#include "Settings.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace VirtualSlots::EffectSources {
namespace {
    constexpr auto kRecord = Serialization::MakeRecordType('E', 'F', 'S', 'R');
    constexpr std::uint32_t kRecordVersion = 1;

    struct Variant {
        Core::ItemSourceKind kind {Core::ItemSourceKind::kFormOnly};
        Core::CustomEnchantmentSignature custom;
        ExtraRingMode mode {ExtraRingMode::kFunctional};
        std::vector<RE::FormID> forms;
    };

    std::mutex g_lock;
    [[nodiscard]] auto& Sources() {
        static std::unordered_map<RE::FormID, std::vector<Variant>> sources;
        return sources;
    }

    [[nodiscard]] bool MatchesVariant(
        const Variant& a_variant,
        const Core::ItemSource& a_source,
        const ExtraRingMode a_mode
    ) {
        return a_variant.kind
               == a_source.kind
               && a_variant.custom
               == a_source.customEnchantment
               && a_variant.mode
               == a_mode;
    }

    void Initialize(RE::TESObjectARMO& a_armor, const RE::TESObjectARMO& a_source, const Variant& a_variant) {
        a_armor.SetSlotMask(RE::BGSBipedObjectForm::BipedObjectSlot::kNone);
        a_armor.armorAddons.clear();
        a_armor.formFlags |= RE::TESObjectARMO::RecordFlags::kNonPlayable;
        a_armor.value = 0;
        a_armor.weight = 0.0F;
        const auto* name = a_source.GetName();
        if (!a_variant.custom.playerDisplayName.empty()) {
            name = a_variant.custom.playerDisplayName.c_str();
        }
        a_armor.SetFullName(name ? name : "");
        const auto inheritEnchantment = a_variant.mode
                                        == ExtraRingMode::kFunctional
                                        && a_variant.kind
                                        == Core::ItemSourceKind::kFormOnly;
        a_armor.formEnchanting = inheritEnchantment ? a_source.formEnchanting : nullptr;
        a_armor.amountofEnchantment = inheritEnchantment ? a_source.amountofEnchantment : 0;
    }

    [[nodiscard]] RE::TESObjectARMO* Create(RE::TESObjectARMO& a_source, const Variant& a_variant) {
        auto* duplicate = a_source.CreateDuplicateForm(true, nullptr);
        auto* armor = duplicate ? duplicate->As<RE::TESObjectARMO>() : nullptr;
        if (!armor) {
            SKSE::log::error("Cannot create an effect source for ring {:08X}", a_source.GetFormID());
            return nullptr;
        }
        Initialize(*armor, a_source, a_variant);
        if (!RE::TESDataHandler::GetSingleton()->AddFormToDataHandler(armor)) {
            SKSE::log::error(
                "Cannot register effect source {:08X} for ring {:08X}",
                armor->GetFormID(),
                a_source.GetFormID()
            );
            return nullptr;
        }
        return armor;
    }

    void RestoreVariant(const RE::FormID a_source, Variant a_variant) {
        const auto* ring = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_source);
        if (!ring) {
            SKSE::log::warn("Cannot restore effect sources for missing ring {:08X}", a_source);
            return;
        }
        // Skyrim restores dynamic form identities without their duplicated armor data.
        for (const auto form : a_variant.forms) {
            if (auto* armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(form)) {
                Initialize(*armor, *ring, a_variant);
            }
        }
        std::scoped_lock const lock(g_lock);
        Sources()[a_source].push_back(std::move(a_variant));
    }

    [[nodiscard]] bool WriteVariant(
        SKSE::SerializationInterface& a_intfc,
        const RE::FormID a_source,
        const Variant& a_variant
    ) {
        using Serialization::WriteField;
        return a_intfc.OpenRecord(kRecord, kRecordVersion)
               && WriteField(a_intfc, a_source)
               && WriteField(a_intfc, std::to_underlying(a_variant.kind))
               && WriteField(a_intfc, std::to_underlying(a_variant.mode))
               && WriteField(a_intfc, a_variant.custom.enchantmentFormID)
               && WriteField(a_intfc, a_variant.custom.charge)
               && WriteField(a_intfc, static_cast<std::uint8_t>(a_variant.custom.removeOnUnequip))
               && Serialization::WriteString(a_intfc, a_variant.custom.playerDisplayName)
               && WriteField(a_intfc, static_cast<std::uint32_t>(a_variant.forms.size()))
               && std::ranges::all_of(a_variant.forms, [&](const auto a_form) { return WriteField(a_intfc, a_form); });
    }
}

bool Matches(const RE::FormID a_effectSource, const Core::ItemSource& a_source, const ExtraRingMode a_mode) {
    std::scoped_lock const lock(g_lock);
    const auto source = Sources().find(a_source.sourceFormID);
    return source != Sources().end() && std::ranges::any_of(source->second, [&](const auto& a_variant) {
        return MatchesVariant(a_variant, a_source, a_mode) && std::ranges::contains(a_variant.forms, a_effectSource);
    });
}

RE::TESObjectARMO* Acquire(
    RE::TESObjectARMO& a_ring,
    const Core::ItemSource& a_source,
    const ExtraRingMode a_mode,
    const std::span<const RE::FormID> a_usedByActor,
    const RE::FormID a_retainedSource
) {
    std::scoped_lock const lock(g_lock);
    auto& variants = Sources()[a_source.sourceFormID];
    auto variant = std::ranges::find_if(variants, [&](const auto& a_variant) {
        return MatchesVariant(a_variant, a_source, a_mode);
    });
    if (variant == variants.end()) {
        variants.push_back({.kind = a_source.kind, .custom = a_source.customEnchantment, .mode = a_mode, .forms = {}});
        variant = std::prev(variants.end());
    }
    if (a_retainedSource
        != 0
        && std::ranges::contains(variant->forms, a_retainedSource)
        && !std::ranges::contains(a_usedByActor, a_retainedSource)) {
        return RE::TESForm::LookupByID<RE::TESObjectARMO>(a_retainedSource);
    }
    // The engine separates duplicate enchantments by source form within each actor.
    // Immutable forms can be shared across actors, but one actor needs a distinct form per equipped copy.
    for (const auto form : variant->forms) {
        if (!std::ranges::contains(a_usedByActor, form)) {
            return RE::TESForm::LookupByID<RE::TESObjectARMO>(form);
        }
    }
    auto* armor = Create(a_ring, *variant);
    if (armor) {
        variant->forms.push_back(armor->GetFormID());
    }
    return armor;
}

void Save(SKSE::SerializationInterface& a_intfc) {
    std::scoped_lock const lock(g_lock);
    for (const auto& [source, variants] : Sources()) {
        for (const auto& variant : variants) {
            if (!variant.forms.empty() && !WriteVariant(a_intfc, source, variant)) {
                SKSE::log::error("Cannot save shared effect sources for ring {:08X}", source);
                return;
            }
        }
    }
}

bool TryLoadRecord(const Serialization::RecordInfo a_record, SKSE::SerializationInterface& a_intfc) {
    if (a_record.type != kRecord) {
        return false;
    }
    auto remaining = a_record.length;
    const auto read = [&] {
        using Serialization::ReadField;
        RE::FormID source = 0;
        Variant variant;
        std::uint32_t kind = 0;
        std::underlying_type_t<ExtraRingMode> mode = 0;
        std::uint8_t removeOnUnequip = 0;
        if (a_record.version
            != kRecordVersion
            || !ReadField(a_intfc, remaining, source)
            || !ReadField(a_intfc, remaining, kind)
            || !ReadField(a_intfc, remaining, mode)
            || !ReadField(a_intfc, remaining, variant.custom.enchantmentFormID)
            || !ReadField(a_intfc, remaining, variant.custom.charge)
            || !ReadField(a_intfc, remaining, removeOnUnequip)) {
            SKSE::log::error("Cannot read effect-source record version {}", a_record.version);
            return;
        }
        const auto name = Serialization::ReadString(a_intfc, remaining);
        std::uint32_t count = 0;
        if (!name || !ReadField(a_intfc, remaining, count) || count > remaining / sizeof(RE::FormID)) {
            SKSE::log::error("Truncated effect-source record for ring {:08X}", source);
            return;
        }
        variant.kind = static_cast<Core::ItemSourceKind>(kind);
        variant.mode = static_cast<ExtraRingMode>(mode);
        variant.custom.playerDisplayName = *name;
        variant.custom.removeOnUnequip = removeOnUnequip != 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            RE::FormID form = 0;
            if (!ReadField(a_intfc, remaining, form)) {
                SKSE::log::error("Truncated effect-source list for ring {:08X}", source);
                return;
            }
            if (a_intfc.ResolveFormID(form, form)) {
                variant.forms.push_back(form);
            }
        }
        const auto originalSource = source;
        if (!a_intfc.ResolveFormID(source, source)
            || (variant.kind
                == Core::ItemSourceKind::kCustomEnchantment
                && !a_intfc.ResolveFormID(variant.custom.enchantmentFormID, variant.custom.enchantmentFormID))) {
            SKSE::log::warn("Cannot resolve saved effect-source variant for ring {:08X}", originalSource);
            return;
        }
        RestoreVariant(source, std::move(variant));
    };
    read();
    Serialization::DrainRecordData(a_intfc, remaining);
    return true;
}

void Revert() {
    std::scoped_lock const lock(g_lock);
    Sources().clear();
}
}
