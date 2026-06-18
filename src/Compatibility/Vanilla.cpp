#include "Compatibility/Vanilla.h"

#include "Inventory.h"
#include "Serialization.h"

#include "RE/S/SpellsLearned.h"
#include "RE/T/TESFile.h"

#include <CLIBUtil/string.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace Compatibility::Vanilla {
namespace {
    constexpr std::string_view kSkyrimPlugin {"Skyrim.esm"};
    constexpr std::string_view kDragonbornPlugin {"Dragonborn.esm"};
    constexpr std::array kOfficialRingDefiningPlugins {
        kSkyrimPlugin,
        std::string_view {"Dawnguard.esm"},
        kDragonbornPlugin,
        std::string_view {"ccbgssse001-fish.esm"},
        std::string_view {"ccbgssse025-advdsgs.esm"},
        std::string_view {"ccbgssse051-ba_daedricmail.esl"},
        std::string_view {"ccedhsse001-norjewel.esl"},
        std::string_view {"cckrtsse001_altar.esl"},
        std::string_view {"ccpewsse002-armsofchaos.esl"},
    };

    constexpr RE::FormID kBondOfMatrimonyFormID {0x000C5809};

    constexpr RE::FormID kWerewolfBeastRaceFormID {0x0CDD84};
    constexpr RE::FormID kWerewolfChangeFormID {0x092C48};
    constexpr RE::FormID kWerewolfChangeRingOfHircineFormID {0x0F8306};

    enum class FrostmoonEffectKind : std::uint8_t {
        kPerk,
        kSpell,
        kCombatSpell,
    };

    enum class TransformApplyResult : std::uint8_t {
        kFailed,
        kAlreadyPresent,
        kApplied,
    };

    struct FrostmoonRingDefinition {
        RE::FormID localFormID {0};
        std::int32_t ringID {0};
    };

    struct FrostmoonEffectDefinition {
        std::int32_t ringID {0};
        RE::FormID localFormID {0};
        FrostmoonEffectKind kind {FrostmoonEffectKind::kSpell};
    };

    struct FrostmoonVirtualState {
        std::vector<std::int32_t> ringIDs;
    };

    struct FrostmoonTransformState {
        std::vector<std::int32_t> pendingRingIDs;
        std::vector<std::int32_t> appliedRingIDs;
    };

    constexpr std::array kFrostmoonRings {
        FrostmoonRingDefinition {.localFormID = 0x0275B6, .ringID = 1}, // Bloodlust
        FrostmoonRingDefinition {.localFormID = 0x0275B8, .ringID = 2}, // Moon
        FrostmoonRingDefinition {.localFormID = 0x0275B7, .ringID = 3}, // Instinct
        FrostmoonRingDefinition {.localFormID = 0x0275B9, .ringID = 4}, // Hunt
    };

    constexpr std::array kFrostmoonEffects {
        FrostmoonEffectDefinition {
            .ringID = 1,
            .localFormID = 0x035B17,
            .kind = FrostmoonEffectKind::kPerk,
        },
        FrostmoonEffectDefinition {
            .ringID = 2,
            .localFormID = 0x035B16,
            .kind = FrostmoonEffectKind::kPerk,
        },
        FrostmoonEffectDefinition {
            .ringID = 3,
            .localFormID = 0x035B1B,
            .kind = FrostmoonEffectKind::kCombatSpell,
        },
        FrostmoonEffectDefinition {
            .ringID = 4,
            .localFormID = 0x035B1E,
            .kind = FrostmoonEffectKind::kSpell,
        },
    };

    constexpr auto kRecordFrostmoonCompatibility = Serialization::MakeRecordType('F', 'M', 'S', 'C');
    constexpr std::uint32_t kFrostmoonCompatibilityVersion = 1;
    constexpr auto kMaxFrostmoonRingIDs = static_cast<std::uint32_t>(kFrostmoonRings.size());

    std::mutex g_lock;
    FrostmoonVirtualState g_virtualState;
    FrostmoonTransformState g_transformState;

    [[nodiscard]] bool ContainsRingID(const std::vector<std::int32_t>& a_ringIDs, const std::int32_t a_ringID) {
        return std::ranges::find(a_ringIDs, a_ringID) != a_ringIDs.end();
    }

    [[nodiscard]] RE::TESObjectARMO* LookupFrostmoonRing(
        RE::TESDataHandler& a_data,
        const FrostmoonRingDefinition& a_ring
    ) {
        return a_data.LookupForm<RE::TESObjectARMO>(a_ring.localFormID, kDragonbornPlugin);
    }

    [[nodiscard]] const FrostmoonRingDefinition* LookupFrostmoonRingDefinition(
        RE::TESDataHandler& a_data,
        const RE::FormID a_sourceFormID
    ) {
        for (const auto& ring : kFrostmoonRings) {
            const auto* resolvedRing = LookupFrostmoonRing(a_data, ring);
            if (resolvedRing && resolvedRing->GetFormID() == a_sourceFormID) {
                return std::addressof(ring);
            }
        }

        return nullptr;
    }

    [[nodiscard]] const FrostmoonEffectDefinition* LookupFrostmoonEffectDefinition(const std::int32_t a_ringID) {
        const auto it = std::ranges::find_if(kFrostmoonEffects, [a_ringID](const auto& a_effect) {
            return a_effect.ringID == a_ringID;
        });

        return it != kFrostmoonEffects.end() ? std::addressof(*it) : nullptr;
    }

    void AddUniqueKnownRingID(std::vector<std::int32_t>& a_ringIDs, const std::int32_t a_ringID) {
        if (LookupFrostmoonEffectDefinition(a_ringID) && !ContainsRingID(a_ringIDs, a_ringID)) {
            a_ringIDs.push_back(a_ringID);
        }
    }

    [[nodiscard]] std::vector<std::int32_t> SanitizeRingIDs(const std::vector<std::int32_t>& a_ringIDs) {
        std::vector<std::int32_t> sanitizedRingIDs;
        sanitizedRingIDs.reserve(std::min(a_ringIDs.size(), kFrostmoonRings.size()));

        for (const auto ringID : a_ringIDs) {
            AddUniqueKnownRingID(sanitizedRingIDs, ringID);
        }

        return sanitizedRingIDs;
    }

    [[nodiscard]] std::optional<std::vector<std::int32_t>> ReadAppliedRingIDs(
        SKSE::SerializationInterface& a_intfc,
        const std::uint32_t a_length
    ) {
        auto remaining = a_length;
        std::uint32_t ringIDCount = 0;
        if (!Serialization::ReadField(a_intfc, remaining, ringIDCount)) {
            Serialization::DrainRecordData(a_intfc, remaining);
            return std::nullopt;
        }

        if (ringIDCount > kMaxFrostmoonRingIDs) {
            logger::warn(
                "Compatibility: Frostmoon load skipped | count={} | max={} | reason=invalidCount",
                ringIDCount,
                kMaxFrostmoonRingIDs
            );
            Serialization::DrainRecordData(a_intfc, remaining);
            return std::nullopt;
        }

        std::vector<std::int32_t> ringIDs;
        ringIDs.reserve(ringIDCount);
        for (std::uint32_t index = 0; index < ringIDCount; ++index) {
            std::int32_t ringID = 0;
            if (!Serialization::ReadField(a_intfc, remaining, ringID)) {
                Serialization::DrainRecordData(a_intfc, remaining);
                return std::nullopt;
            }

            AddUniqueKnownRingID(ringIDs, ringID);
        }

        if (remaining != 0) {
            logger::warn("Compatibility: Frostmoon load skipped | remaining={} | reason=unexpectedData", remaining);
            Serialization::DrainRecordData(a_intfc, remaining);
            return std::nullopt;
        }

        return ringIDs;
    }

    [[nodiscard]] std::vector<std::int32_t> ResolveVirtualFrostmoonRingIDs(
        RE::TESDataHandler& a_data,
        std::span<const RE::FormID> a_virtualRingSourceFormIDs
    ) {
        std::vector<std::int32_t> ringIDs;
        ringIDs.reserve(kFrostmoonRings.size());

        for (const auto sourceFormID : a_virtualRingSourceFormIDs) {
            if (sourceFormID == 0) {
                continue;
            }

            const auto* ring = LookupFrostmoonRingDefinition(a_data, sourceFormID);
            if (ring && !ContainsRingID(ringIDs, ring->ringID)) {
                ringIDs.push_back(ring->ringID);
            }
        }

        return ringIDs;
    }

    [[nodiscard]] bool IsFrostmoonRingRightWorn(
        RE::Actor& a_actor,
        RE::TESDataHandler& a_data,
        const std::int32_t a_ringID
    ) {
        const auto it = std::ranges::find_if(kFrostmoonRings, [a_ringID](const auto& a_ring) {
            return a_ring.ringID == a_ringID;
        });
        if (it == kFrostmoonRings.end()) {
            return false;
        }

        auto* ring = LookupFrostmoonRing(a_data, *it);
        return ring && Inventory::GetRingInventoryState(a_actor, *ring).rightWorn;
    }

    [[nodiscard]] std::vector<std::int32_t> FilterRightWornDuplicates(
        RE::Actor& a_actor,
        RE::TESDataHandler& a_data,
        const std::vector<std::int32_t>& a_ringIDs
    ) {
        std::vector<std::int32_t> filteredRingIDs;
        filteredRingIDs.reserve(a_ringIDs.size());

        for (const auto ringID : a_ringIDs) {
            if (!IsFrostmoonRingRightWorn(a_actor, a_data, ringID)) {
                filteredRingIDs.push_back(ringID);
            }
        }

        return filteredRingIDs;
    }

    [[nodiscard]] bool IsWerewolf(RE::Actor& a_actor, RE::TESDataHandler& a_data) {
        const auto* werewolfRace = a_data.LookupForm<RE::TESRace>(kWerewolfBeastRaceFormID, kSkyrimPlugin);
        return werewolfRace && a_actor.GetRace() == werewolfRace;
    }

    using DoCombatSpellApply_t = void (*)(RE::Actor*, RE::SpellItem*, RE::TESObjectREFR*);

    void DoCombatSpellApply(RE::Actor& a_actor, RE::SpellItem& a_spell) {
        // DLC2dunFrostmoonPlayerScript.ApplyRingEffect: player.DoCombatSpellApply
        REL::Relocation<DoCombatSpellApply_t> doCombatSpellApply {RELOCATION_ID(37666, 38620)};
        doCombatSpellApply(std::addressof(a_actor), std::addressof(a_spell), std::addressof(a_actor));
    }

    [[nodiscard]] bool HasActiveSpellEffect(RE::Actor& a_actor, const RE::SpellItem& a_spell) {
        auto* magicTarget = a_actor.AsMagicTarget();
        if (!magicTarget) {
            return false;
        }

        bool found {false};
        magicTarget->VisitActiveEffects([&](RE::ActiveEffect* a_activeEffect) -> RE::BSContainer::ForEachResult {
            if (a_activeEffect
                && !a_activeEffect->flags.any(RE::ActiveEffect::Flag::kDispelled)
                && a_activeEffect->spell
                == std::addressof(a_spell)) {
                found = true;
                return RE::BSContainer::ForEachResult::kStop;
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        return found;
    }

    void DispelSpellEffects(RE::Actor& a_actor, const RE::SpellItem& a_spell) {
        auto* magicTarget = a_actor.AsMagicTarget();
        if (!magicTarget) {
            return;
        }

        std::vector<RE::ActiveEffect*> effects;
        magicTarget->VisitActiveEffects([&](RE::ActiveEffect* a_activeEffect) -> RE::BSContainer::ForEachResult {
            if (a_activeEffect
                && !a_activeEffect->flags.any(RE::ActiveEffect::Flag::kDispelled)
                && a_activeEffect->spell
                == std::addressof(a_spell)) {
                effects.push_back(a_activeEffect);
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        for (auto* effect : effects) {
            effect->Dispel(true);
        }
    }

    TransformApplyResult ApplyFrostmoonTransformEffect(
        RE::TESDataHandler& a_data,
        RE::Actor& a_actor,
        const std::int32_t a_ringID
    ) {
        const auto* definition = LookupFrostmoonEffectDefinition(a_ringID);
        if (!definition) {
            logger::warn(
                "Compatibility: Frostmoon transform apply skipped | ringID={} | reason=missingDefinition",
                a_ringID
            );
            return TransformApplyResult::kFailed;
        }

        switch (definition->kind) {
            case FrostmoonEffectKind::kPerk: {
                auto* perk = a_data.LookupForm<RE::BGSPerk>(definition->localFormID, kDragonbornPlugin);
                if (!perk) {
                    logger::warn(
                        "Compatibility: Frostmoon transform apply skipped | ringID={} | form={:08X} | reason=missingPerk",
                        a_ringID,
                        definition->localFormID
                    );
                    return TransformApplyResult::kFailed;
                }

                if (a_actor.HasPerk(perk)) {
                    return TransformApplyResult::kAlreadyPresent;
                }

                a_actor.AddPerk(perk);
                return TransformApplyResult::kApplied;
            }
            case FrostmoonEffectKind::kSpell: {
                auto* spell = a_data.LookupForm<RE::SpellItem>(definition->localFormID, kDragonbornPlugin);
                if (!spell) {
                    logger::warn(
                        "Compatibility: Frostmoon transform apply skipped | ringID={} | form={:08X} | reason=missingSpell",
                        a_ringID,
                        definition->localFormID
                    );
                    return TransformApplyResult::kFailed;
                }

                if (a_actor.HasSpell(spell)) {
                    return TransformApplyResult::kAlreadyPresent;
                }

                if (!a_actor.AddSpell(spell)) {
                    return TransformApplyResult::kFailed;
                }

                RE::SpellsLearned::SendEvent(spell);
                return TransformApplyResult::kApplied;
            }
            case FrostmoonEffectKind::kCombatSpell: {
                auto* spell = a_data.LookupForm<RE::SpellItem>(definition->localFormID, kDragonbornPlugin);
                if (!spell) {
                    logger::warn(
                        "Compatibility: Frostmoon transform apply skipped | ringID={} | form={:08X} | reason=missingSpell",
                        a_ringID,
                        definition->localFormID
                    );
                    return TransformApplyResult::kFailed;
                }

                if (HasActiveSpellEffect(a_actor, *spell)) {
                    return TransformApplyResult::kAlreadyPresent;
                }

                DoCombatSpellApply(a_actor, *spell);
                return HasActiveSpellEffect(a_actor, *spell) ? TransformApplyResult::kApplied
                                                             : TransformApplyResult::kFailed;
            }
        }

        return TransformApplyResult::kFailed;
    }

    void RemoveFrostmoonTransformEffect(RE::TESDataHandler& a_data, RE::Actor& a_actor, const std::int32_t a_ringID) {
        const auto* definition = LookupFrostmoonEffectDefinition(a_ringID);
        if (!definition) {
            return;
        }

        switch (definition->kind) {
            case FrostmoonEffectKind::kPerk: {
                auto* perk = a_data.LookupForm<RE::BGSPerk>(definition->localFormID, kDragonbornPlugin);
                if (perk && a_actor.HasPerk(perk)) {
                    a_actor.RemovePerk(perk);
                }
                break;
            }
            case FrostmoonEffectKind::kSpell: {
                auto* spell = a_data.LookupForm<RE::SpellItem>(definition->localFormID, kDragonbornPlugin);
                if (spell && a_actor.HasSpell(spell)) {
                    a_actor.RemoveSpell(spell);
                }
                break;
            }
            case FrostmoonEffectKind::kCombatSpell: {
                auto* spell = a_data.LookupForm<RE::SpellItem>(definition->localFormID, kDragonbornPlugin);
                if (spell) {
                    DispelSpellEffects(a_actor, *spell);
                }
                break;
            }
        }
    }

    void RemoveAppliedFrostmoonTransformEffects(RE::TESDataHandler& a_data, RE::Actor& a_actor) {
        std::vector<std::int32_t> appliedRingIDs;
        {
            std::scoped_lock lock(g_lock);
            appliedRingIDs = std::move(g_transformState.appliedRingIDs);
            g_transformState.appliedRingIDs.clear();
        }

        for (const auto ringID : appliedRingIDs) {
            RemoveFrostmoonTransformEffect(a_data, a_actor, ringID);
        }
    }

    void ReconcileAppliedFrostmoonTransformEffects(
        RE::TESDataHandler& a_data,
        RE::Actor& a_actor,
        const std::vector<std::int32_t>& a_virtualRingIDs
    ) {
        if (!IsWerewolf(a_actor, a_data)) {
            return;
        }

        const auto desiredRingIDs = FilterRightWornDuplicates(a_actor, a_data, a_virtualRingIDs);
        std::vector<std::int32_t> appliedRingIDs;
        {
            std::scoped_lock lock(g_lock);
            appliedRingIDs = g_transformState.appliedRingIDs;
        }

        std::vector<std::int32_t> reconciledRingIDs;
        reconciledRingIDs.reserve(desiredRingIDs.size());

        for (const auto ringID : appliedRingIDs) {
            if (ContainsRingID(desiredRingIDs, ringID)) {
                AddUniqueKnownRingID(reconciledRingIDs, ringID);
                continue;
            }

            RemoveFrostmoonTransformEffect(a_data, a_actor, ringID);
        }

        for (const auto ringID : desiredRingIDs) {
            if (ContainsRingID(reconciledRingIDs, ringID)) {
                continue;
            }

            if (ApplyFrostmoonTransformEffect(a_data, a_actor, ringID) == TransformApplyResult::kApplied) {
                reconciledRingIDs.push_back(ringID);
            }
        }

        {
            std::scoped_lock lock(g_lock);
            g_transformState.appliedRingIDs = std::move(reconciledRingIDs);
        }
    }
}

bool IsOfficialRingDefiningFile(const RE::TESFile* a_file) {
    if (!a_file) {
        return false;
    }

    const auto filename = a_file->GetFilename();
    return std::ranges::any_of(kOfficialRingDefiningPlugins, [filename](const std::string_view a_plugin) {
        return clib_util::string::iequals(filename, a_plugin);
    });
}

bool IsBondOfMatrimony(const RE::TESObjectARMO* a_ring) {
    return a_ring && a_ring->GetFormID() == kBondOfMatrimonyFormID;
}

void RefreshFrostmoonVirtualRings(
    const Core::ActorKey a_actor,
    std::span<const RE::FormID> a_virtualRingSourceFormIDs
) {
    if (!Core::IsPlayerActorKey(a_actor)) {
        return;
    }

    auto* data = RE::TESDataHandler::GetSingleton();
    if (!data) {
        return;
    }

    const auto ringIDs = ResolveVirtualFrostmoonRingIDs(*data, a_virtualRingSourceFormIDs);
    {
        std::scoped_lock lock(g_lock);
        if (g_virtualState.ringIDs != ringIDs) {
            g_virtualState.ringIDs = ringIDs;
        }
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (player) {
        ReconcileAppliedFrostmoonTransformEffects(*data, *player, ringIDs);
    }
}

void HandleSpellCast(RE::Actor& a_actor, const RE::FormID a_spellFormID) {
    if (!a_actor.IsPlayerRef()) {
        return;
    }

    auto* data = RE::TESDataHandler::GetSingleton();
    if (!data) {
        return;
    }

    const auto* werewolfChange = data->LookupForm<RE::SpellItem>(kWerewolfChangeFormID, kSkyrimPlugin);
    const auto* hircineChange = data->LookupForm<RE::SpellItem>(kWerewolfChangeRingOfHircineFormID, kSkyrimPlugin);
    const auto isWerewolfChange = (werewolfChange && werewolfChange->GetFormID() == a_spellFormID)
                                  || (hircineChange && hircineChange->GetFormID() == a_spellFormID);
    if (!isWerewolfChange) {
        return;
    }

    RemoveAppliedFrostmoonTransformEffects(*data, a_actor);

    std::vector<std::int32_t> virtualRingIDs;
    {
        std::scoped_lock lock(g_lock);
        virtualRingIDs = g_virtualState.ringIDs;
    }

    auto pendingRingIDs = FilterRightWornDuplicates(a_actor, *data, virtualRingIDs);
    {
        std::scoped_lock lock(g_lock);
        g_transformState.pendingRingIDs = std::move(pendingRingIDs);
    }
}

void HandleRaceSwitchComplete(RE::Actor& a_actor) {
    if (!a_actor.IsPlayerRef()) {
        return;
    }

    auto* data = RE::TESDataHandler::GetSingleton();
    if (!data) {
        return;
    }

    if (!IsWerewolf(a_actor, *data)) {
        {
            std::scoped_lock lock(g_lock);
            g_transformState.pendingRingIDs.clear();
        }
        RemoveAppliedFrostmoonTransformEffects(*data, a_actor);
        return;
    }

    std::vector<std::int32_t> pendingRingIDs;
    std::vector<std::int32_t> appliedRingIDs;
    {
        std::scoped_lock lock(g_lock);
        pendingRingIDs = std::move(g_transformState.pendingRingIDs);
        g_transformState.pendingRingIDs.clear();
        appliedRingIDs = g_transformState.appliedRingIDs;
    }
    if (pendingRingIDs.empty()) {
        return;
    }

    for (const auto ringID : pendingRingIDs) {
        if (ContainsRingID(appliedRingIDs, ringID)) {
            continue;
        }

        const auto applyResult = ApplyFrostmoonTransformEffect(*data, a_actor, ringID);
        if (applyResult == TransformApplyResult::kApplied) {
            appliedRingIDs.push_back(ringID);
        }
    }

    {
        std::scoped_lock lock(g_lock);
        g_transformState.appliedRingIDs = std::move(appliedRingIDs);
    }
}

void Save(SKSE::SerializationInterface& a_intfc) {
    std::vector<std::int32_t> appliedRingIDs;
    {
        std::scoped_lock lock(g_lock);
        appliedRingIDs = SanitizeRingIDs(g_transformState.appliedRingIDs);
    }

    if (appliedRingIDs.empty()) {
        return;
    }

    if (!a_intfc.OpenRecord(kRecordFrostmoonCompatibility, kFrostmoonCompatibilityVersion)) {
        logger::error("Compatibility: Frostmoon save failed | reason=openRecord");
        return;
    }

    const auto ringIDCount = static_cast<std::uint32_t>(appliedRingIDs.size());
    if (!Serialization::WriteField(a_intfc, ringIDCount)) {
        logger::error("Compatibility: Frostmoon save failed | reason=writeCount");
        return;
    }

    for (const auto ringID : appliedRingIDs) {
        if (!Serialization::WriteField(a_intfc, ringID)) {
            logger::error("Compatibility: Frostmoon save failed | ringID={} | reason=writeRingID", ringID);
            return;
        }
    }
}

bool TryLoadRecord(const Serialization::RecordInfo a_recordInfo, SKSE::SerializationInterface& a_intfc) {
    if (a_recordInfo.type != kRecordFrostmoonCompatibility) {
        return false;
    }

    if (a_recordInfo.version != kFrostmoonCompatibilityVersion) {
        logger::warn(
            "Compatibility: Frostmoon load skipped | version={} | length={} | reason=unsupportedVersion",
            a_recordInfo.version,
            a_recordInfo.length
        );
        Serialization::DrainRecordData(a_intfc, a_recordInfo.length);
        return true;
    }

    auto appliedRingIDs = ReadAppliedRingIDs(a_intfc, a_recordInfo.length);
    if (!appliedRingIDs) {
        logger::error("Compatibility: Frostmoon load failed | reason=readRecord");
        return true;
    }

    {
        std::scoped_lock lock(g_lock);
        g_transformState.pendingRingIDs.clear();
        g_transformState.appliedRingIDs = std::move(*appliedRingIDs);
    }
    return true;
}

void Revert() {
    std::scoped_lock lock(g_lock);
    g_virtualState = {};
    g_transformState = {};
}
}
