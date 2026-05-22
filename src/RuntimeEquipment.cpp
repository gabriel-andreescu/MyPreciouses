#include "RuntimeEquipment.h"

#include "EventBindings.h"
#include "Inventory.h"
#include "RuntimeClones.h"
#include "Selection.h"
#include "Settings.h"
#include "Slots.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
constexpr auto kDummyRingModelPath = R"(LeftHandRingsSKSE\Rings\DummyRing_1.nif)";
constexpr std::array<RE::SEX, RE::SEXES::kTotal> kSexes {RE::SEXES::kMale, RE::SEXES::kFemale};

struct RuntimeArmor {
    RE::FormID originalFormID {0};
    RE::TESObjectARMO* armor {nullptr};
    std::vector<RE::TESObjectARMA*> addons;
    std::unordered_map<RE::TESObjectARMA*, RuntimeEquipment::AddonModel> addonModels;
    RE::VMHandle eventTargetHandle {0};
    bool restoredFromSave {false};
};

struct EnchantmentPowerState {
    RE::EnchantmentItem* enchantment {nullptr};
    std::uint32_t percent {100};
    std::uint16_t charge {0};
    bool removeOnUnequip {false};
    bool custom {false};
    bool hasPlayerDisplayName {false};
    std::string playerDisplayName;

    [[nodiscard]] bool operator==(const EnchantmentPowerState&) const = default;
};

struct EnchantmentPowerSource {
    RE::EnchantmentItem* enchantment {nullptr};
    std::uint16_t charge {0};
    bool removeOnUnequip {false};
    bool custom {false};
    std::optional<std::string> playerDisplayName;
};

struct CustomSelectionSource {
    RE::ExtraDataList* extraList {nullptr};
    Inventory::CustomEnchantmentData customData;
};

struct ChannelState {
    std::unordered_map<RE::FormID, RuntimeArmor> runtimeArmors;
    RE::FormID equippedOriginalFormID {0};
    RE::TESObjectARMO* equippedArmor {nullptr};
    RE::ExtraDataList* equippedExtraList {nullptr};
    Selection::State equippedSelection;
    EnchantmentPowerState equippedEnchantmentPower;
    bool hasEquippedEnchantmentPower {false};
    bool equippedArmorRestoredFromSave {false};
};

[[nodiscard]] std::array<ChannelState, kDisplaySlots.size()>& ChannelStates() {
    static auto* states = new std::array<ChannelState, kDisplaySlots.size()>();
    return *states;
}

std::mutex g_lock;
bool g_refreshPending {false};
bool g_refreshRunning {false};

[[nodiscard]] bool SourceMatchesGetEquippedArgument(
    const RE::FormID a_sourceFormID,
    RE::TESForm& a_getEquippedArgument
) {
    if (a_sourceFormID == 0) {
        return false;
    }

    if (auto* list = a_getEquippedArgument.As<RE::BGSListForm>()) {
        return list->HasForm(a_sourceFormID);
    }

    return a_getEquippedArgument.GetFormID() == a_sourceFormID;
}

[[nodiscard]] bool IsFingerRingVisualAddon(const RE::TESObjectARMA& a_addon) {
    return a_addon.GetSlotMask() == RE::BGSBipedObjectForm::BipedObjectSlot::kRing;
}

void RefreshOnce();
void RefreshChannel(DisplaySlot a_channel, ChannelState& a_state);
[[nodiscard]] RuntimeArmor* EnsureRuntimeArmor(DisplaySlot a_channel, ChannelState& a_state, RE::TESObjectARMO& a_ring);
[[nodiscard]] RuntimeArmor* EnsureLoadedRuntimeArmorRestored(
    DisplaySlot a_channel,
    ChannelState& a_state,
    RE::TESObjectARMO& a_ring
);
[[nodiscard]] std::optional<RuntimeArmor> RestoreLoadedRuntimeArmor(
    DisplaySlot a_channel,
    RE::TESObjectARMO& a_ring,
    const RuntimeClones::CloneRecord& a_record
);
[[nodiscard]] bool EquipRuntimeArmor(
    RE::Actor& a_actor,
    DisplaySlot a_channel,
    ChannelState& a_state,
    RuntimeArmor& a_runtimeArmor,
    const Selection::State& a_selection,
    bool a_deferWornCheck = false
);
[[nodiscard]] bool EnsureRuntimeArmorInventory(RE::Actor& a_actor, RuntimeArmor& a_runtimeArmor);
[[nodiscard]] bool ApplyCustomSelectionToRuntime(
    RE::Actor& a_actor,
    DisplaySlot a_channel,
    RuntimeArmor& a_runtimeArmor,
    const Selection::State& a_selection,
    RE::InventoryEntryData*& a_entry,
    RE::ExtraDataList*& a_extraList,
    const CustomSelectionSource* a_customSource = nullptr
);
[[nodiscard]] bool ResolveEnchantmentPowerState(
    RE::Actor& a_actor,
    DisplaySlot a_channel,
    RuntimeArmor& a_runtimeArmor,
    const Selection::State& a_selection,
    EnchantmentPowerState& a_state,
    const CustomSelectionSource* a_customSource = nullptr
);
[[nodiscard]] bool ResolveEnchantmentPowerSource(
    RE::Actor& a_actor,
    DisplaySlot a_channel,
    RuntimeArmor& a_runtimeArmor,
    const Selection::State& a_selection,
    EnchantmentPowerSource& a_source,
    const CustomSelectionSource* a_customSource = nullptr
);
[[nodiscard]] std::optional<CustomSelectionSource> ResolveCustomSelectionSource(
    RE::Actor& a_actor,
    DisplaySlot a_channel,
    RuntimeArmor& a_runtimeArmor,
    const Selection::State& a_selection,
    bool a_clearSelectionOnCustomDataFailure
);
[[nodiscard]] std::uint32_t GetEffectiveEnchantmentPowerPercent(RE::Actor& a_actor);
[[nodiscard]] bool ReapplyEquippedEnchantmentPower(RE::Actor& a_actor, DisplaySlot a_channel, ChannelState& a_state);
[[nodiscard]] std::optional<bool> TryAdoptEquippedRestoredRuntimeArmor(
    RE::Actor& a_actor,
    DisplaySlot a_channel,
    ChannelState& a_state,
    RuntimeArmor& a_runtimeArmor,
    const Selection::State& a_selection,
    RE::InventoryEntryData* a_entry,
    RE::ExtraDataList* a_extraList,
    const EnchantmentPowerState& a_enchantmentPowerState
);
void ArmRuntimeEquipEventMirror(
    RE::Actor& a_actor,
    RuntimeArmor& a_runtimeArmor,
    std::optional<EventBindings::ScopedMirror>& a_mirrorContext
);
void EquipRuntimeArmorObject(
    RE::Actor& a_actor,
    RE::ActorEquipManager& a_equipManager,
    RuntimeArmor& a_runtimeArmor,
    RE::ExtraDataList* a_extraList
);
[[nodiscard]] std::optional<std::string_view> ConsumeLoadedFailureReason(const RuntimeArmor& a_runtimeArmor);
[[nodiscard]] bool FailLoadedRestoreDuringEquip(
    RE::Actor& a_actor,
    DisplaySlot a_channel,
    ChannelState& a_state,
    RuntimeArmor& a_runtimeArmor,
    RE::ExtraDataList* a_extraList,
    std::string_view a_reason
);
[[nodiscard]] bool AdoptAlreadyEquippedRestoredArmor(
    RE::Actor& a_actor,
    DisplaySlot a_channel,
    ChannelState& a_state,
    RuntimeArmor& a_runtimeArmor,
    RE::InventoryEntryData* a_entry,
    RE::ExtraDataList* a_extraList
);
RuntimeArmor* FailLoadedRestore(DisplaySlot a_channel, RE::FormID a_originalFormID, std::string_view a_reason);
[[nodiscard]] bool IsArmorWorn(RE::Actor& a_actor, const RE::TESObjectARMO& a_armor);
[[nodiscard]] bool IsArmorWorn(RE::Actor& a_actor, const RuntimeArmor& a_runtimeArmor);
void UnequipActive(RE::Actor& a_actor, ChannelState& a_state);
[[nodiscard]] bool EquippedSelectionMatches(const ChannelState& a_state, const Selection::State& a_selection);
void RecordEquippedSelection(ChannelState& a_state, const Selection::State& a_selection);
void RecordEquippedEnchantmentPower(ChannelState& a_state, const EnchantmentPowerState& a_powerState);
void ClearEquippedSelection(ChannelState& a_state);
void ClearEquippedEnchantmentPower(ChannelState& a_state);

[[nodiscard]] const char* GetModel(const RE::TESModel& a_model) {
    const auto* model = a_model.GetModel();
    return model && model[0] != '\0' ? model : nullptr;
}

[[nodiscard]] RuntimeEquipment::AddonModel::Model CaptureModel(const RE::TESModelTextureSwap& a_model) {
    RuntimeEquipment::AddonModel::Model model;
    if (const auto* path = GetModel(a_model)) {
        model.path = path;
        model.textureSwap = std::addressof(a_model);
        model.alternateTextures = a_model.alternateTextures;
        model.numAlternateTextures = a_model.numAlternateTextures;
    }

    return model;
}

[[nodiscard]] std::optional<RuntimeEquipment::AddonModel> CaptureAddonModels(
    const RE::TESObjectARMA& a_sourceAddon,
    const DisplaySlot a_channel
) {
    RuntimeEquipment::AddonModel metadata;
    metadata.channel = a_channel;
    metadata.sourceAddon = std::addressof(a_sourceAddon);
    bool foundAnyModel = false;

    for (const auto sex : kSexes) {
        const auto sexIndex = std::to_underlying(sex);
        auto& thirdPersonModel = metadata.thirdPerson[sexIndex];
        auto& firstPersonModel = metadata.firstPerson[sexIndex];

        thirdPersonModel = CaptureModel(a_sourceAddon.bipedModels[sexIndex]);
        firstPersonModel = CaptureModel(a_sourceAddon.bipedModel1stPersons[sexIndex]);

        if (!thirdPersonModel.path.empty()) {
            foundAnyModel = true;
        }

        if (!firstPersonModel.path.empty()) {
            foundAnyModel = true;
        }
    }

    if (!foundAnyModel) {
        return std::nullopt;
    }

    return metadata;
}

void ApplyRuntimeAddonDummyModels(RE::TESObjectARMA& a_clonedAddon, const RuntimeEquipment::AddonModel& a_metadata) {
    for (const auto sex : kSexes) {
        const auto sexIndex = std::to_underlying(sex);
        if (!a_metadata.thirdPerson[sexIndex].path.empty()) {
            a_clonedAddon.bipedModels[sexIndex].SetModel(kDummyRingModelPath);
        }

        if (!a_metadata.firstPerson[sexIndex].path.empty()) {
            a_clonedAddon.bipedModel1stPersons[sexIndex].SetModel(kDummyRingModelPath);
        }
    }
}

[[nodiscard]] std::optional<RuntimeEquipment::AddonModel> PrepareAddonModels(
    const RE::TESObjectARMA& a_sourceAddon,
    RE::TESObjectARMA& a_clonedAddon,
    const DisplaySlot a_channel
) {
    auto metadata = CaptureAddonModels(a_sourceAddon, a_channel);
    if (!metadata) {
        return std::nullopt;
    }

    ApplyRuntimeAddonDummyModels(a_clonedAddon, *metadata);
    return metadata;
}

struct RuntimeAddonEntry {
    RE::TESObjectARMA* sourceAddon {nullptr};
    RE::TESObjectARMA* attachedAddon {nullptr};
    std::optional<RuntimeEquipment::AddonModel> metadata;
};

[[nodiscard]] std::optional<RuntimeAddonEntry> CreateRuntimeAddonEntry(
    RE::TESObjectARMA& a_sourceAddon,
    const RE::FormID a_sourceArmorFormID,
    const DisplaySlot a_channel
) {
    if (!IsFingerRingVisualAddon(a_sourceAddon)) {
        return RuntimeAddonEntry {
            .sourceAddon = std::addressof(a_sourceAddon),
            .attachedAddon = std::addressof(a_sourceAddon),
        };
    }

    auto* runtimeAddon = RuntimeClones::DuplicateAddon(a_sourceAddon);
    if (!runtimeAddon) {
        return std::nullopt;
    }

    RuntimeClones::ConfigureAddon(a_sourceAddon, *runtimeAddon, a_channel);
    RuntimeClones::CopyRaceCoverage(a_sourceAddon, *runtimeAddon);

    auto metadata = PrepareAddonModels(a_sourceAddon, *runtimeAddon, a_channel);
    if (!metadata) {
        return std::nullopt;
    }

    if (!RuntimeClones::RegisterForm(*runtimeAddon, "ARMA"sv, a_sourceArmorFormID)) {
        return std::nullopt;
    }

    return RuntimeAddonEntry {
        .sourceAddon = std::addressof(a_sourceAddon),
        .attachedAddon = runtimeAddon,
        .metadata = std::move(metadata),
    };
}

[[nodiscard]] std::optional<std::vector<RE::TESObjectARMA*>> ResolveRuntimeAddonSources(
    const RE::TESObjectARMO& a_sourceArmor,
    const RuntimeClones::CloneRecord& a_record
) {
    if (a_record.sourceAddonFormIDs.empty()) {
        return std::nullopt;
    }

    std::vector<RE::TESObjectARMA*> sourceAddons;
    sourceAddons.reserve(a_record.sourceAddonFormIDs.size());
    auto nextSourceIndex = std::uint32_t {0};
    for (const auto storedSourceAddonFormID : a_record.sourceAddonFormIDs) {
        RE::TESObjectARMA* matchedSourceAddon = nullptr;
        auto matchedIndex = std::uint32_t {0};
        for (auto sourceIndex = nextSourceIndex; sourceIndex < a_sourceArmor.armorAddons.size(); ++sourceIndex) {
            auto* sourceAddon = a_sourceArmor.armorAddons[sourceIndex];
            if (sourceAddon && sourceAddon->GetFormID() == storedSourceAddonFormID) {
                matchedSourceAddon = sourceAddon;
                matchedIndex = sourceIndex;
                break;
            }
        }

        if (!matchedSourceAddon) {
            return std::nullopt;
        }

        sourceAddons.push_back(matchedSourceAddon);
        nextSourceIndex = matchedIndex + 1;
    }

    return sourceAddons;
}

[[nodiscard]] bool IsLeftRuntimeArmor(const RE::TESObjectARMO& a_armor, const DisplaySlot a_channel) {
    return a_armor.HasPartOf(Slots::GetArmorSlot(a_channel))
           && !a_armor.HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kRing);
}

void QueueRuntimeEquipmentRefresh() {
    stl::add_task([] {
        RuntimeEquipment::RequestRefresh();
    });
}

[[nodiscard]] float GetEnchantmentScaleForPercent(const std::uint32_t a_percent) {
    if (a_percent >= Settings::kDefaultEnchantmentPowerPercent) {
        return 1.0F;
    }

    return static_cast<float>(a_percent) / static_cast<float>(Settings::kDefaultEnchantmentPowerPercent);
}

[[nodiscard]] bool MagnitudesMatch(const float a_lhs, const float a_rhs) {
    constexpr auto tolerance = 0.01F;
    return std::fabs(a_lhs - a_rhs) <= tolerance;
}

[[nodiscard]] bool RuntimeActiveEffectMagnitudesMatchScale(
    RE::Actor& a_actor,
    const RE::TESObjectARMO* a_runtimeArmor,
    const RE::EnchantmentItem* a_enchantment,
    const std::uint32_t a_percent
) {
    auto* magicTarget = a_actor.AsMagicTarget();
    if (!magicTarget) {
        return false;
    }

    auto* activeEffects = magicTarget->GetActiveEffectList();
    if (!activeEffects) {
        return false;
    }

    const auto scale = GetEnchantmentScaleForPercent(a_percent);
    std::uint32_t sourceEffects = 0;
    for (const auto* activeEffect : *activeEffects) {
        if (!activeEffect || activeEffect->source != a_runtimeArmor) {
            continue;
        }

        ++sourceEffects;
        if (activeEffect->spell != a_enchantment) {
            return false;
        }

        if (!activeEffect->effect) {
            return false;
        }

        const auto baseMagnitude = activeEffect->effect->effectItem.magnitude;
        const auto expectedMagnitude = baseMagnitude * scale;
        if (!MagnitudesMatch(activeEffect->magnitude, expectedMagnitude)) {
            return false;
        }
    }

    return !a_enchantment || sourceEffects > 0;
}

void SyncRuntimeArmorName(RE::TESObjectARMO& a_runtime, const std::string_view a_name) {
    const std::string nextName(a_name);
    a_runtime.SetFullName(nextName.c_str());
}

void SyncRuntimeArmorMetadata(const RE::TESObjectARMO& a_source, RE::TESObjectARMO& a_runtime) {
    const auto* sourceName = a_source.GetName();

    a_runtime.SetFullName(sourceName ? sourceName : "");
    a_runtime.formEnchanting = a_source.formEnchanting;
    a_runtime.amountofEnchantment = a_source.amountofEnchantment;
}

struct RightHandEnchantmentState {
    RE::TESObjectARMO* ring {nullptr};
    bool enchanted {false};
};

[[nodiscard]] RightHandEnchantmentState InspectRightHandEnchantment(RE::Actor& a_actor) {
    RightHandEnchantmentState result;
    result.ring = Inventory::AsRing(a_actor.GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kRing));
    if (!result.ring) {
        return result;
    }

    const auto state = Inventory::GetSourceState(a_actor, *result.ring);
    result.enchanted = state.HasRightWornEnchantment();

    return result;
}

[[nodiscard]] RE::ExtraDataList* FindExtraList(RE::InventoryEntryData* a_entry, RE::ExtraDataList* a_preferred) {
    if (!a_entry || !a_entry->extraLists) {
        return nullptr;
    }

    if (a_preferred) {
        for (auto* extraList : *a_entry->extraLists) {
            if (extraList == a_preferred) {
                return extraList;
            }
        }
    }

    for (auto* extraList : *a_entry->extraLists) {
        if (extraList && extraList->GetCount() > 0) {
            return extraList;
        }
    }

    for (auto* extraList : *a_entry->extraLists) {
        if (extraList) {
            return extraList;
        }
    }

    return nullptr;
}

[[nodiscard]] bool HasRuntimeArmorBipedPart(
    RE::Actor& a_actor,
    const DisplaySlot a_channel,
    const RE::TESObjectARMO& a_armor
) {
    bool sawBiped = false;
    for (const auto firstPerson : {false, true}) {
        const auto& biped = a_actor.GetBiped(firstPerson);
        if (!biped) {
            continue;
        }

        sawBiped = true;
        const auto& bipedObject = biped->objects[Slots::GetBipedObject(a_channel)];
        if (bipedObject.item == std::addressof(a_armor) && bipedObject.partClone) {
            return true;
        }
    }

    return !sawBiped;
}

void ResetActor3DForWornArmor(RE::Actor& a_actor) {
    a_actor.DoReset3D(false);
}

void QueueActor3DResetForWornRuntimeArmor(
    RE::Actor& a_actor,
    const DisplaySlot a_channel,
    const RE::TESObjectARMO& a_armor
) {
    const auto actorHandle = a_actor.GetHandle();
    const auto armorFormID = a_armor.GetFormID();

    stl::add_task([actorHandle, a_channel, armorFormID] {
        const auto actor = actorHandle.get();
        if (!actor) {
            return;
        }

        auto* armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(armorFormID);
        if (!armor) {
            return;
        }

        auto* wornArmor = actor->GetWornArmor(Slots::GetArmorSlot(a_channel));
        if (wornArmor != armor) {
            return;
        }

        ResetActor3DForWornArmor(*actor);
    });
}
}

void RuntimeEquipment::RequestRefresh() {
    bool shouldQueue = false;
    {
        std::scoped_lock lock(g_lock);
        if (g_refreshPending) {
            return;
        }

        g_refreshPending = true;
        shouldQueue = !g_refreshRunning;
    }

    if (shouldQueue) {
        stl::add_task([] {
            RuntimeEquipment::Refresh();
        });
    }
}

void RuntimeEquipment::Refresh() {
    {
        std::scoped_lock lock(g_lock);
        if (g_refreshRunning) {
            g_refreshPending = true;
            return;
        }

        g_refreshRunning = true;
        g_refreshPending = false;
    }

    for (;;) {
        RefreshOnce();

        std::scoped_lock lock(g_lock);
        if (!g_refreshPending) {
            g_refreshRunning = false;
            return;
        }

        g_refreshPending = false;
    }
}

namespace {
void RefreshOnce() {
    for (const auto channel : kDisplaySlots) {
        if (channel == DisplaySlot::kBond && !Settings::GetSingleton()->IsBondOfMatrimonyEnabled()) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                UnequipActive(*player, ChannelStates()[ToIndex(channel)]);
            } else {
                ChannelStates()[ToIndex(channel)] = {};
            }
            continue;
        }

        RefreshChannel(channel, ChannelStates()[ToIndex(channel)]);
    }
}

void RefreshChannel(const DisplaySlot a_channel, ChannelState& a_state) {
    auto* player = RE::PlayerCharacter::GetSingleton();
    const auto selection = Selection::Get(a_channel);
    auto* ring = Selection::GetSource(a_channel);

    if (!player || !ring || selection.kind == Selection::Kind::kNone) {
        if (player) {
            UnequipActive(*player, a_state);
        } else {
            a_state.equippedOriginalFormID = 0;
            a_state.equippedArmor = nullptr;
            a_state.equippedExtraList = nullptr;
            ClearEquippedSelection(a_state);
            a_state.equippedArmorRestoredFromSave = false;
        }
        return;
    }

    auto* runtimeArmor = EnsureRuntimeArmor(a_channel, a_state, *ring);
    if (!runtimeArmor) {
        logger::warn("RuntimeEquipment: runtime armor prepare failed | source={:08X}", ring->GetFormID());
        UnequipActive(*player, a_state);
        return;
    }

    if (a_state.equippedOriginalFormID
        != runtimeArmor->originalFormID
        || a_state.equippedArmor
        != runtimeArmor->armor
        || !EquippedSelectionMatches(a_state, selection)) {
        UnequipActive(*player, a_state);
        if (!EquipRuntimeArmor(*player, a_channel, a_state, *runtimeArmor, selection)) {
            logger::warn("RuntimeEquipment: runtime armor equip failed | source={:08X}", runtimeArmor->originalFormID);
        }
        return;
    }

    if (!IsArmorWorn(*player, *runtimeArmor)) {
        if (!EquipRuntimeArmor(*player, a_channel, a_state, *runtimeArmor, selection)) {
            logger::warn(
                "RuntimeEquipment: runtime armor re-equip failed | source={:08X}",
                runtimeArmor->originalFormID
            );
        }
        return;
    }

    if (selection.kind == Selection::Kind::kCustomEnchantment) {
        auto* entry = Inventory::FindEntry(*player, *runtimeArmor->armor);
        auto* extraList = FindExtraList(entry, a_state.equippedExtraList);
        if (!ApplyCustomSelectionToRuntime(*player, a_channel, *runtimeArmor, selection, entry, extraList)) {
            UnequipActive(*player, a_state);
            return;
        }

        a_state.equippedExtraList = extraList;
    }

    if (!ReapplyEquippedEnchantmentPower(*player, a_channel, a_state)) {
        UnequipActive(*player, a_state);
        return;
    }

    if (!HasRuntimeArmorBipedPart(*player, a_channel, *runtimeArmor->armor)) {
        QueueActor3DResetForWornRuntimeArmor(*player, a_channel, *runtimeArmor->armor);
    }
}
}

void RuntimeEquipment::Clear(const DisplaySlot a_channel) {
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto& state = ChannelStates()[ToIndex(a_channel)];
    if (player) {
        UnequipActive(*player, state);
    } else {
        state.equippedOriginalFormID = 0;
        state.equippedArmor = nullptr;
        state.equippedExtraList = nullptr;
        ClearEquippedSelection(state);
        state.equippedArmorRestoredFromSave = false;
    }
}

std::optional<RE::FormID> RuntimeEquipment::SyncAfterEquip(RE::Actor& a_actor, const DisplaySlot a_channel) {
    auto& state = ChannelStates()[ToIndex(a_channel)];
    if (!state.equippedArmor || state.equippedOriginalFormID == 0) {
        return std::nullopt;
    }

    if (IsArmorWorn(a_actor, *state.equippedArmor)) {
        return std::nullopt;
    }

    const auto sourceFormID = state.equippedOriginalFormID;
    Selection::Clear(a_channel);
    UnequipActive(a_actor, state);
    return sourceFormID;
}

void RuntimeEquipment::DiscardState(const DisplaySlot a_channel) {
    std::scoped_lock lock(g_lock);
    auto& state = ChannelStates()[ToIndex(a_channel)];
    state = {};
}

void RuntimeEquipment::DiscardState() {
    std::scoped_lock lock(g_lock);
    for (auto& state : ChannelStates()) {
        state = {};
    }
}

bool RuntimeEquipment::IsMatchingCloneWorn(RE::Actor& a_actor, RE::TESForm& a_getEquippedArgument) {
    for (const auto channel : kDisplaySlots) {
        auto* wornArmor = a_actor.GetWornArmor(Slots::GetArmorSlot(channel));
        if (!wornArmor) {
            continue;
        }

        const auto sourceFormID = RuntimeClones::FindSourceArmorFormID(*wornArmor);
        if (sourceFormID && SourceMatchesGetEquippedArgument(*sourceFormID, a_getEquippedArgument)) {
            return true;
        }
    }

    return false;
}

bool RuntimeEquipment::IsArmor(const RE::TESObjectARMO* a_armor) {
    if (!a_armor) {
        return false;
    }

    std::scoped_lock lock(g_lock);
    return std::ranges::any_of(ChannelStates(), [a_armor](const auto& a_state) {
        return std::ranges::any_of(a_state.runtimeArmors, [a_armor](const auto& a_entry) {
            return a_entry.second.armor == a_armor;
        });
    });
}

float RuntimeEquipment::GetEnchantmentScale(RE::Actor& a_actor, const RE::TESObjectARMO* a_source) {
    const auto runtimeSource = IsArmor(a_source);
    if (!runtimeSource) {
        return 1.0F;
    }

    const auto percent = GetEffectiveEnchantmentPowerPercent(a_actor);
    return GetEnchantmentScaleForPercent(percent);
}

bool RuntimeEquipment::IsAddon(const RE::TESObjectARMO* a_armor, const RE::TESObjectARMA* a_addon) {
    if (!a_armor || !a_addon) {
        return false;
    }

    std::scoped_lock lock(g_lock);
    for (const auto& state : ChannelStates()) {
        for (const auto& [_, runtimeArmor] : state.runtimeArmors) {
            if (runtimeArmor.armor != a_armor) {
                continue;
            }

            return std::ranges::contains(runtimeArmor.addons, const_cast<RE::TESObjectARMA*>(a_addon));
        }
    }

    return false;
}

std::optional<RuntimeEquipment::AddonModel> RuntimeEquipment::GetAddonModel(
    const RE::TESObjectARMO* a_armor,
    const RE::TESObjectARMA* a_addon
) {
    if (!a_armor || !a_addon) {
        return std::nullopt;
    }

    std::scoped_lock lock(g_lock);
    for (const auto& state : ChannelStates()) {
        for (const auto& [_, runtimeArmor] : state.runtimeArmors) {
            if (runtimeArmor.armor != a_armor) {
                continue;
            }

            const auto it = runtimeArmor.addonModels.find(const_cast<RE::TESObjectARMA*>(a_addon));
            if (it != runtimeArmor.addonModels.end()) {
                return it->second;
            }
        }
    }

    return std::nullopt;
}

namespace {
RuntimeArmor* EnsureRuntimeArmor(const DisplaySlot a_channel, ChannelState& a_state, RE::TESObjectARMO& a_ring) {
    const auto originalFormID = a_ring.GetFormID();
    if (auto it = a_state.runtimeArmors.find(originalFormID); it != a_state.runtimeArmors.end()) {
        if (it->second.armor) {
            SyncRuntimeArmorMetadata(a_ring, *it->second.armor);
        }
        return std::addressof(it->second);
    }

    if (RuntimeClones::RequiresRestore(a_channel, originalFormID)) {
        const auto hasRestoreRecord = RuntimeClones::GetRecord(a_channel, originalFormID).has_value();
        if (auto* restoredArmor = EnsureLoadedRuntimeArmorRestored(a_channel, a_state, a_ring)) {
            return restoredArmor;
        }
        if (hasRestoreRecord) {
            return nullptr;
        }
    }

    auto* clonedArmor = RuntimeClones::DuplicateArmor(a_ring);
    if (!clonedArmor) {
        return nullptr;
    }

    RuntimeClones::ConfigureArmor(a_ring, *clonedArmor, a_channel);
    SyncRuntimeArmorMetadata(a_ring, *clonedArmor);
    clonedArmor->armorAddons.clear();

    RuntimeArmor runtimeArmor {
        .originalFormID = originalFormID,
        .armor = clonedArmor,
    };
    RuntimeClones::CloneRecord cloneRecord {
        .channel = a_channel,
        .sourceArmorFormID = originalFormID,
    };

    for (auto* sourceAddon : a_ring.armorAddons) {
        if (!sourceAddon) {
            continue;
        }

        auto addonEntry = CreateRuntimeAddonEntry(*sourceAddon, originalFormID, a_channel);
        if (!addonEntry) {
            continue;
        }

        runtimeArmor.addons.push_back(addonEntry->attachedAddon);
        if (addonEntry->metadata) {
            runtimeArmor.addonModels.emplace(addonEntry->attachedAddon, std::move(*addonEntry->metadata));
        }
        clonedArmor->armorAddons.push_back(addonEntry->attachedAddon);
        cloneRecord.sourceAddonFormIDs.push_back(addonEntry->sourceAddon->GetFormID());
    }

    if (runtimeArmor.addons.empty()) {
        logger::warn(
            "RuntimeEquipment: runtime armor prepare failed | source={:08X} | reason=noUsableAddons",
            originalFormID
        );
        return nullptr;
    }

    if (!RuntimeClones::RegisterForm(*clonedArmor, "ARMO"sv, originalFormID)) {
        return nullptr;
    }
    cloneRecord.cloneArmorFormID = clonedArmor->GetFormID();

    RuntimeClones::Register(cloneRecord);
    auto [it, inserted] = a_state.runtimeArmors.try_emplace(originalFormID, std::move(runtimeArmor));
    return inserted ? std::addressof(it->second) : nullptr;
}

RuntimeArmor* EnsureLoadedRuntimeArmorRestored(
    const DisplaySlot a_channel,
    ChannelState& a_state,
    RE::TESObjectARMO& a_ring
) {
    const auto originalFormID = a_ring.GetFormID();
    if (!RuntimeClones::RequiresRestore(a_channel, originalFormID)) {
        return nullptr;
    }

    const auto record = RuntimeClones::GetRecord(a_channel, originalFormID);
    if (!record) {
        logger::info("RuntimeEquipment: clone restore skipped | source={:08X} | reason=noRecord", originalFormID);
        RuntimeClones::ClearRestore(a_channel, originalFormID);
        return nullptr;
    }

    auto restoredArmor = RestoreLoadedRuntimeArmor(a_channel, a_ring, *record);
    if (!restoredArmor) {
        return FailLoadedRestore(a_channel, originalFormID, "restoreValidationFailed"sv);
    }

    RuntimeClones::MarkRestored(a_channel, originalFormID);
    auto [it, inserted] = a_state.runtimeArmors.try_emplace(originalFormID, std::move(*restoredArmor));
    if (inserted && it->second.armor) {
        SyncRuntimeArmorMetadata(a_ring, *it->second.armor);
    }
    return inserted ? std::addressof(it->second) : nullptr;
}

std::optional<RuntimeArmor> RestoreLoadedRuntimeArmor(
    const DisplaySlot a_channel,
    RE::TESObjectARMO& a_ring,
    const RuntimeClones::CloneRecord& a_record
) {
    const auto originalFormID = a_ring.GetFormID();
    if (a_record.channel != a_channel || a_record.sourceArmorFormID != originalFormID) {
        return std::nullopt;
    }

    auto* clone = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_record.cloneArmorFormID);
    if (!clone) {
        return std::nullopt;
    }

    if (clone == std::addressof(a_ring) || clone->IsDeleted()) {
        return std::nullopt;
    }

    auto sourceAddons = ResolveRuntimeAddonSources(a_ring, a_record);
    if (!sourceAddons) {
        return std::nullopt;
    }

    if (!EventBindings::ValidateLoadedRestore(a_ring, clone->GetFormID())) {
        return std::nullopt;
    }

    RuntimeArmor runtimeArmor {
        .originalFormID = originalFormID,
        .armor = clone,
        .restoredFromSave = true,
    };
    RuntimeClones::CloneRecord refreshedRecord {
        .channel = a_channel,
        .sourceArmorFormID = originalFormID,
        .cloneArmorFormID = clone->GetFormID(),
    };

    std::vector<RuntimeAddonEntry> addonEntries;
    addonEntries.reserve(sourceAddons->size());
    for (auto* sourceAddon : *sourceAddons) {
        if (!sourceAddon) {
            return std::nullopt;
        }

        auto addonEntry = CreateRuntimeAddonEntry(*sourceAddon, originalFormID, a_channel);
        if (!addonEntry) {
            return std::nullopt;
        }

        addonEntries.push_back(std::move(*addonEntry));
    }

    if (addonEntries.empty()) {
        return std::nullopt;
    }

    RuntimeClones::ConfigureArmor(a_ring, *clone, a_channel);
    clone->armorAddons.clear();

    for (auto& addonEntry : addonEntries) {
        runtimeArmor.addons.push_back(addonEntry.attachedAddon);
        if (addonEntry.metadata) {
            runtimeArmor.addonModels.emplace(addonEntry.attachedAddon, std::move(*addonEntry.metadata));
        }
        clone->armorAddons.push_back(addonEntry.attachedAddon);
        refreshedRecord.sourceAddonFormIDs.push_back(addonEntry.sourceAddon->GetFormID());
    }

    if (!IsLeftRuntimeArmor(*clone, a_channel)) {
        return std::nullopt;
    }

    RuntimeClones::Register(refreshedRecord);
    return runtimeArmor;
}

RuntimeArmor* FailLoadedRestore(
    const DisplaySlot a_channel,
    const RE::FormID a_originalFormID,
    const std::string_view a_reason
) {
    logger::warn("RuntimeEquipment: clone restore closed | source={:08X} | reason={}", a_originalFormID, a_reason);
    RuntimeClones::Discard(a_channel, a_originalFormID);
    if (auto* player = RE::PlayerCharacter::GetSingleton()) {
        EventBindings::RemoveForSource(a_originalFormID, *player);
    } else {
        EventBindings::RemoveForSource(a_originalFormID);
    }
    Selection::Clear(a_channel);
    return nullptr;
}

bool FailLoadedRestoreDuringEquip(
    RE::Actor& a_actor,
    const DisplaySlot a_channel,
    ChannelState& a_state,
    RuntimeArmor& a_runtimeArmor,
    RE::ExtraDataList* a_extraList,
    const std::string_view a_reason
) {
    logger::warn(
        "RuntimeEquipment: clone restore closed | source={:08X} | clone={:08X} | phase=papyrusAdoption | reason={}",
        a_runtimeArmor.originalFormID,
        a_runtimeArmor.armor->GetFormID(),
        a_reason
    );

    auto* failedEntry = Inventory::FindEntry(a_actor, *a_runtimeArmor.armor);
    auto* failedExtraList = FindExtraList(failedEntry, a_extraList);
    const auto failedCount = Inventory::GetCount(a_actor, *a_runtimeArmor.armor);
    if (failedCount > 0) {
        a_actor
            .RemoveItem(a_runtimeArmor.armor, failedCount, RE::ITEM_REMOVE_REASON::kRemove, failedExtraList, nullptr);
    }

    ResetActor3DForWornArmor(a_actor);
    RuntimeClones::Discard(a_channel, a_runtimeArmor.originalFormID);
    EventBindings::RemoveForSource(a_runtimeArmor.originalFormID, a_actor);
    Selection::Clear(a_channel);
    a_state.equippedOriginalFormID = 0;
    a_state.equippedArmor = nullptr;
    a_state.equippedExtraList = nullptr;
    ClearEquippedSelection(a_state);
    a_state.equippedArmorRestoredFromSave = false;
    return false;
}

bool AdoptAlreadyEquippedRestoredArmor(
    RE::Actor& a_actor,
    const DisplaySlot a_channel,
    ChannelState& a_state,
    RuntimeArmor& a_runtimeArmor,
    RE::InventoryEntryData* a_entry,
    RE::ExtraDataList* a_extraList
) {
    auto* sourceRing = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_runtimeArmor.originalFormID);
    if (!sourceRing) {
        return FailLoadedRestoreDuringEquip(
            a_actor,
            a_channel,
            a_state,
            a_runtimeArmor,
            a_extraList,
            "sourceMissingForAlreadyEquippedAdoption"sv
        );
    }

    if (EventBindings::HasLoadedBinding(a_runtimeArmor.originalFormID, a_runtimeArmor.armor->GetFormID())
        && !EventBindings::AdoptLoadedBinding(*sourceRing, a_runtimeArmor.armor->GetFormID())) {
        return FailLoadedRestoreDuringEquip(
            a_actor,
            a_channel,
            a_state,
            a_runtimeArmor,
            a_extraList,
            "directAdoptionValidationFailed"sv
        );
    }

    if (const auto handle = EventBindings::GetHandle(
            a_runtimeArmor.originalFormID,
            a_runtimeArmor.armor->GetFormID()
        )) {
        a_runtimeArmor.eventTargetHandle = *handle;
    }

    a_state.equippedExtraList = FindExtraList(a_entry, a_extraList);
    if (!HasRuntimeArmorBipedPart(a_actor, a_channel, *a_runtimeArmor.armor)) {
        QueueActor3DResetForWornRuntimeArmor(a_actor, a_channel, *a_runtimeArmor.armor);
    }

    a_state.equippedOriginalFormID = a_runtimeArmor.originalFormID;
    a_state.equippedArmor = a_runtimeArmor.armor;
    a_state.equippedArmorRestoredFromSave = true;

    return IsArmorWorn(a_actor, a_runtimeArmor);
}

bool EnsureRuntimeArmorInventory(RE::Actor& a_actor, RuntimeArmor& a_runtimeArmor) {
    auto count = Inventory::GetCount(a_actor, *a_runtimeArmor.armor);
    if (count > 0) {
        return true;
    }

    a_actor.AddObjectToContainer(a_runtimeArmor.armor, nullptr, 1, nullptr);
    count = Inventory::GetCount(a_actor, *a_runtimeArmor.armor);
    if (count <= 0) {
        logger::warn(
            "RuntimeEquipment: runtime armor inventory add failed | armor={:08X} | source={:08X}",
            a_runtimeArmor.armor->GetFormID(),
            a_runtimeArmor.originalFormID
        );
        return false;
    }

    return true;
}

void ArmRuntimeEquipEventMirror(
    RE::Actor& a_actor,
    RuntimeArmor& a_runtimeArmor,
    std::optional<EventBindings::ScopedMirror>& a_mirrorContext
) {
    auto* sourceRing = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_runtimeArmor.originalFormID);
    if (!sourceRing) {
        return;
    }

    const auto adoptLoadedBinding = a_runtimeArmor.restoredFromSave
                                    && EventBindings::HasLoadedBinding(
                                        a_runtimeArmor.originalFormID,
                                        a_runtimeArmor.armor->GetFormID()
                                    );

    if (!EventBindings::BeginPendingMirror(
            *sourceRing,
            *a_runtimeArmor.armor,
            RE::BSFixedString {"OnEquipped"},
            adoptLoadedBinding,
            a_actor.GetFormID()
        )) {
        return;
    }
    a_mirrorContext.emplace(*sourceRing, *a_runtimeArmor.armor, adoptLoadedBinding);
}

std::uint32_t GetEffectiveEnchantmentPowerPercent(RE::Actor& a_actor) {
    const auto configuredPercent = Settings::GetSingleton()->GetEnchantmentPowerPercent();
    const auto rightHand = InspectRightHandEnchantment(a_actor);
    const auto effectivePercent = configuredPercent >= Settings::kDefaultEnchantmentPowerPercent || !rightHand.enchanted
                                      ? Settings::kDefaultEnchantmentPowerPercent
                                      : configuredPercent;

    if (effectivePercent >= Settings::kDefaultEnchantmentPowerPercent) {
        return Settings::kDefaultEnchantmentPowerPercent;
    }

    return effectivePercent;
}

bool ApplyCustomSelectionToRuntime(
    RE::Actor& a_actor,
    const DisplaySlot a_channel,
    RuntimeArmor& a_runtimeArmor,
    const Selection::State& a_selection,
    RE::InventoryEntryData*& a_entry,
    RE::ExtraDataList*& a_extraList,
    const CustomSelectionSource* a_customSource
) {
    if (a_selection.kind != Selection::Kind::kCustomEnchantment) {
        return true;
    }

    const auto customKey = a_selection.GetCustomKey();
    std::optional<CustomSelectionSource> resolvedSource;
    const auto* source = a_customSource;
    if (!source) {
        resolvedSource = ResolveCustomSelectionSource(a_actor, a_channel, a_runtimeArmor, a_selection, false);
        if (!resolvedSource) {
            return false;
        }

        source = std::addressof(*resolvedSource);
    }

    if (!a_extraList) {
        auto* inventoryChanges = a_actor.GetInventoryChanges();
        if (!inventoryChanges) {
            return false;
        }

        a_extraList = inventoryChanges->EnchantObject(
            a_runtimeArmor.armor,
            nullptr,
            source->customData.enchantment,
            source->customData.charge
        );
        a_entry = Inventory::FindEntry(a_actor, *a_runtimeArmor.armor);
    }

    if (!a_extraList) {
        return false;
    }

    if (!Inventory::MirrorCustomEnchantment(*a_extraList, *source->extraList)) {
        Selection::Clear(a_channel);
        return false;
    }

    if (source->customData.playerDisplayName) {
        SyncRuntimeArmorName(*a_runtimeArmor.armor, *source->customData.playerDisplayName);
    }

    return true;
}

std::optional<CustomSelectionSource> ResolveCustomSelectionSource(
    RE::Actor& a_actor,
    const DisplaySlot a_channel,
    RuntimeArmor& a_runtimeArmor,
    const Selection::State& a_selection,
    const bool a_clearSelectionOnCustomDataFailure
) {
    const auto customKey = a_selection.GetCustomKey();
    auto* sourceRing = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_runtimeArmor.originalFormID);
    if (!sourceRing) {
        Selection::Clear(a_channel);
        return std::nullopt;
    }

    const auto sourceMatches = Inventory::FindSourceMatches(a_actor, *sourceRing, customKey);
    auto* sourceExtraList = sourceMatches.firstExtraList;
    if (!sourceExtraList) {
        Selection::Clear(a_channel);
        return std::nullopt;
    }

    if (!Inventory::MatchesCustomEnchantmentKey(sourceExtraList, customKey)) {
        Selection::Clear(a_channel);
        return std::nullopt;
    }

    auto customData = Inventory::ReadCustomEnchantment(*sourceExtraList);
    if (!customData) {
        if (a_clearSelectionOnCustomDataFailure) {
            Selection::Clear(a_channel);
        }
        return std::nullopt;
    }

    return CustomSelectionSource {
        .extraList = sourceExtraList,
        .customData = std::move(*customData),
    };
}

bool ResolveEnchantmentPowerState(
    RE::Actor& a_actor,
    const DisplaySlot a_channel,
    RuntimeArmor& a_runtimeArmor,
    const Selection::State& a_selection,
    EnchantmentPowerState& a_state,
    const CustomSelectionSource* a_customSource
) {
    a_state = EnchantmentPowerState {};
    a_state.percent = GetEffectiveEnchantmentPowerPercent(a_actor);

    EnchantmentPowerSource source;
    if (!ResolveEnchantmentPowerSource(a_actor, a_channel, a_runtimeArmor, a_selection, source, a_customSource)) {
        return false;
    }

    if (!source.enchantment) {
        return true;
    }

    a_state.enchantment = source.enchantment;
    a_state.charge = source.charge;
    a_state.removeOnUnequip = source.removeOnUnequip;
    a_state.custom = source.custom;
    if (source.playerDisplayName) {
        a_state.hasPlayerDisplayName = true;
        a_state.playerDisplayName = *source.playerDisplayName;
    }

    return true;
}

bool ResolveEnchantmentPowerSource(
    RE::Actor& a_actor,
    const DisplaySlot a_channel,
    RuntimeArmor& a_runtimeArmor,
    const Selection::State& a_selection,
    EnchantmentPowerSource& a_source,
    const CustomSelectionSource* a_customSource
) {
    a_source = EnchantmentPowerSource {};
    if (a_selection.kind == Selection::Kind::kCustomEnchantment) {
        std::optional<CustomSelectionSource> resolvedSource;
        const auto* source = a_customSource;
        if (!source) {
            resolvedSource = ResolveCustomSelectionSource(a_actor, a_channel, a_runtimeArmor, a_selection, true);
            if (!resolvedSource) {
                return false;
            }

            source = std::addressof(*resolvedSource);
        }

        a_source.enchantment = source->customData.enchantment;
        a_source.charge = source->customData.charge;
        a_source.removeOnUnequip = source->customData.removeOnUnequip;
        a_source.custom = true;
        a_source.playerDisplayName = source->customData.playerDisplayName;
        return true;
    }

    auto* sourceRing = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_runtimeArmor.originalFormID);
    if (!sourceRing) {
        Selection::Clear(a_channel);
        return false;
    }

    a_source.enchantment = sourceRing->formEnchanting;
    a_source.charge = sourceRing->amountofEnchantment;
    return true;
}

bool ReapplyEquippedEnchantmentPower(RE::Actor& a_actor, const DisplaySlot a_channel, ChannelState& a_state) {
    if (!a_state.equippedArmor || a_state.equippedOriginalFormID == 0) {
        return true;
    }

    const auto runtimeIt = a_state.runtimeArmors.find(a_state.equippedOriginalFormID);
    if (runtimeIt == a_state.runtimeArmors.end()) {
        return true;
    }

    auto& runtimeArmor = runtimeIt->second;
    if (!IsArmorWorn(a_actor, runtimeArmor)) {
        return true;
    }

    const auto selection = Selection::Get(a_channel);
    if (selection.kind == Selection::Kind::kNone || !selection.MatchesSource(a_state.equippedOriginalFormID)) {
        return true;
    }

    EnchantmentPowerState nextState;
    if (!ResolveEnchantmentPowerState(a_actor, a_channel, runtimeArmor, selection, nextState)) {
        UnequipActive(a_actor, a_state);
        return false;
    }

    const auto previousState = a_state.hasEquippedEnchantmentPower ? a_state.equippedEnchantmentPower
                                                                   : EnchantmentPowerState {};
    if (previousState == nextState) {
        return true;
    }

    UnequipActive(a_actor, a_state);
    return EquipRuntimeArmor(a_actor, a_channel, a_state, runtimeArmor, selection, true);
}

std::optional<bool> TryAdoptEquippedRestoredRuntimeArmor(
    RE::Actor& a_actor,
    const DisplaySlot a_channel,
    ChannelState& a_state,
    RuntimeArmor& a_runtimeArmor,
    const Selection::State& a_selection,
    RE::InventoryEntryData* a_entry,
    RE::ExtraDataList* a_extraList,
    const EnchantmentPowerState& a_enchantmentPowerState
) {
    if (!a_runtimeArmor.restoredFromSave || !IsArmorWorn(a_actor, a_runtimeArmor)) {
        return std::nullopt;
    }

    const auto
        adopted = AdoptAlreadyEquippedRestoredArmor(a_actor, a_channel, a_state, a_runtimeArmor, a_entry, a_extraList);
    if (!adopted) {
        return false;
    }

    RecordEquippedSelection(a_state, a_selection);
    if (RuntimeActiveEffectMagnitudesMatchScale(
            a_actor,
            a_runtimeArmor.armor,
            a_enchantmentPowerState.enchantment,
            a_enchantmentPowerState.percent
        )) {
        RecordEquippedEnchantmentPower(a_state, a_enchantmentPowerState);
        return true;
    }

    ClearEquippedEnchantmentPower(a_state);
    return ReapplyEquippedEnchantmentPower(a_actor, a_channel, a_state);
}

void EquipRuntimeArmorObject(
    RE::Actor& a_actor,
    RE::ActorEquipManager& a_equipManager,
    RuntimeArmor& a_runtimeArmor,
    RE::ExtraDataList* a_extraList
) {
    std::optional<EventBindings::ScopedMirror> mirrorContext;
    const auto restoredFreshMirror = a_runtimeArmor.restoredFromSave
                                     && !EventBindings::HasLoadedBinding(
                                         a_runtimeArmor.originalFormID,
                                         a_runtimeArmor.armor->GetFormID()
                                     );
    ArmRuntimeEquipEventMirror(a_actor, a_runtimeArmor, mirrorContext);

    a_equipManager
        .EquipObject(std::addressof(a_actor), a_runtimeArmor.armor, a_extraList, 1, nullptr, true, false, false, true);

    if (const auto handle = EventBindings::GetHandle(
            a_runtimeArmor.originalFormID,
            a_runtimeArmor.armor->GetFormID()
        )) {
        a_runtimeArmor.eventTargetHandle = *handle;
    } else if (restoredFreshMirror && a_runtimeArmor.eventTargetHandle != 0) {
        auto* sourceRing = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_runtimeArmor.originalFormID);
        if (sourceRing) {
            static_cast<void>(EventBindings::MirrorScriptsAndDispatch(
                *sourceRing,
                a_runtimeArmor.armor->GetFormID(),
                a_runtimeArmor.eventTargetHandle,
                a_actor,
                RE::BSFixedString {"OnEquipped"}
            ));
        }
    }
}

std::optional<std::string_view> ConsumeLoadedFailureReason(const RuntimeArmor& a_runtimeArmor) {
    const auto loadedAdoptionFailed = EventBindings::ConsumeLoadedFailure(
        a_runtimeArmor.originalFormID,
        a_runtimeArmor.armor->GetFormID()
    );
    if (loadedAdoptionFailed) {
        return "adoptionValidationFailed"sv;
    }

    const auto loadedAdoptionMissing = a_runtimeArmor.restoredFromSave
                                       && EventBindings::HasLoadedBinding(
                                           a_runtimeArmor.originalFormID,
                                           a_runtimeArmor.armor->GetFormID()
                                       );
    if (loadedAdoptionMissing) {
        return "adoptionEventNotObserved"sv;
    }

    return std::nullopt;
}

bool EquipRuntimeArmor(
    RE::Actor& a_actor,
    const DisplaySlot a_channel,
    ChannelState& a_state,
    RuntimeArmor& a_runtimeArmor,
    const Selection::State& a_selection,
    const bool a_deferWornCheck
) {
    if (!a_runtimeArmor.armor || !IsLeftRuntimeArmor(*a_runtimeArmor.armor, a_channel)) {
        return false;
    }

    if (!a_actor.GetInventoryChanges()) {
        return false;
    }

    if (!EnsureRuntimeArmorInventory(a_actor, a_runtimeArmor)) {
        return false;
    }

    auto* entry = Inventory::FindEntry(a_actor, *a_runtimeArmor.armor);
    auto* extraList = FindExtraList(entry, a_state.equippedExtraList);
    std::optional<CustomSelectionSource> customSource;
    const CustomSelectionSource* customSourcePtr = nullptr;
    if (a_selection.kind == Selection::Kind::kCustomEnchantment) {
        customSource = ResolveCustomSelectionSource(a_actor, a_channel, a_runtimeArmor, a_selection, false);
        if (!customSource) {
            return false;
        }

        customSourcePtr = std::addressof(*customSource);
    }

    EnchantmentPowerState enchantmentPowerState;
    if (!ApplyCustomSelectionToRuntime(
            a_actor,
            a_channel,
            a_runtimeArmor,
            a_selection,
            entry,
            extraList,
            customSourcePtr
        )) {
        return false;
    }

    if (!ResolveEnchantmentPowerState(
            a_actor,
            a_channel,
            a_runtimeArmor,
            a_selection,
            enchantmentPowerState,
            customSourcePtr
        )) {
        return false;
    }

    if (const auto adopted = TryAdoptEquippedRestoredRuntimeArmor(
            a_actor,
            a_channel,
            a_state,
            a_runtimeArmor,
            a_selection,
            entry,
            extraList,
            enchantmentPowerState
        )) {
        return *adopted;
    }

    auto* equipManager = RE::ActorEquipManager::GetSingleton();
    if (!equipManager) {
        return false;
    }

    EquipRuntimeArmorObject(a_actor, *equipManager, a_runtimeArmor, extraList);

    if (const auto failureReason = ConsumeLoadedFailureReason(a_runtimeArmor)) {
        return FailLoadedRestoreDuringEquip(a_actor, a_channel, a_state, a_runtimeArmor, extraList, *failureReason);
    }

    entry = Inventory::FindEntry(a_actor, *a_runtimeArmor.armor);
    a_state.equippedExtraList = FindExtraList(entry, extraList);

    const auto equipped = IsArmorWorn(a_actor, a_runtimeArmor);
    if (!equipped && !a_deferWornCheck) {
        return false;
    }

    if (!equipped) {
        QueueRuntimeEquipmentRefresh();
    } else {
        ResetActor3DForWornArmor(a_actor);
    }

    a_state.equippedOriginalFormID = a_runtimeArmor.originalFormID;
    a_state.equippedArmor = a_runtimeArmor.armor;
    RecordEquippedSelection(a_state, a_selection);
    RecordEquippedEnchantmentPower(a_state, enchantmentPowerState);
    a_state.equippedArmorRestoredFromSave = a_runtimeArmor.restoredFromSave;

    const auto hasBipedPart = equipped ? HasRuntimeArmorBipedPart(a_actor, a_channel, *a_runtimeArmor.armor) : true;
    return hasBipedPart;
}

bool IsArmorWorn(RE::Actor& a_actor, const RE::TESObjectARMO& a_armor) {
    const auto slotMask = a_armor.GetSlotMask();
    for (auto index = std::uint32_t {0}; index < RE::BIPED_OBJECTS::kEditorTotal; ++index) {
        const auto slot = Slots::ToArmorSlot(static_cast<RE::BIPED_OBJECTS::BIPED_OBJECT>(index));
        if (slotMask.all(slot) && a_actor.GetWornArmor(slot) == std::addressof(a_armor)) {
            return true;
        }
    }

    return false;
}

bool IsArmorWorn(RE::Actor& a_actor, const RuntimeArmor& a_runtimeArmor) {
    if (!a_runtimeArmor.armor) {
        return false;
    }

    return IsArmorWorn(a_actor, *a_runtimeArmor.armor);
}

bool EquippedSelectionMatches(const ChannelState& a_state, const Selection::State& a_selection) {
    return a_state.equippedSelection == a_selection;
}

void RecordEquippedSelection(ChannelState& a_state, const Selection::State& a_selection) {
    a_state.equippedSelection = a_selection;
}

void RecordEquippedEnchantmentPower(ChannelState& a_state, const EnchantmentPowerState& a_powerState) {
    a_state.equippedEnchantmentPower = a_powerState;
    a_state.hasEquippedEnchantmentPower = true;
}

void ClearEquippedSelection(ChannelState& a_state) {
    a_state.equippedSelection = {};
    ClearEquippedEnchantmentPower(a_state);
}

void ClearEquippedEnchantmentPower(ChannelState& a_state) {
    a_state.equippedEnchantmentPower = {};
    a_state.hasEquippedEnchantmentPower = false;
}

void UnequipActive(RE::Actor& a_actor, ChannelState& a_state) {
    if (!a_state.equippedArmor) {
        a_state.equippedOriginalFormID = 0;
        a_state.equippedExtraList = nullptr;
        ClearEquippedSelection(a_state);
        a_state.equippedArmorRestoredFromSave = false;
        return;
    }

    auto* equippedArmor = a_state.equippedArmor;
    auto* equipManager = RE::ActorEquipManager::GetSingleton();
    auto* entry = Inventory::FindEntry(a_actor, *equippedArmor);
    auto* extraList = FindExtraList(entry, a_state.equippedExtraList);
    const auto count = Inventory::GetCount(a_actor, *equippedArmor);

    if (equipManager && count > 0) {
        {
            auto* sourceRing = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_state.equippedOriginalFormID);
            std::optional<EventBindings::ScopedMirror> mirrorContext;
            if (sourceRing && !a_state.equippedArmorRestoredFromSave) {
                if (EventBindings::BeginPendingMirror(
                        *sourceRing,
                        *equippedArmor,
                        RE::BSFixedString {"OnUnequipped"},
                        false,
                        a_actor.GetFormID()
                    )) {
                    mirrorContext.emplace(*sourceRing, *equippedArmor);
                }
            }

            equipManager->UnequipObject(
                std::addressof(a_actor),
                equippedArmor,
                extraList,
                1,
                nullptr,
                true,
                false,
                false,
                true,
                nullptr
            );
        }
    }

    const auto countAfterUnequip = Inventory::GetCount(a_actor, *equippedArmor);
    if (countAfterUnequip > 0) {
        entry = Inventory::FindEntry(a_actor, *equippedArmor);
        extraList = countAfterUnequip == 1 ? FindExtraList(entry, extraList) : nullptr;
        a_actor.RemoveItem(equippedArmor, countAfterUnequip, RE::ITEM_REMOVE_REASON::kRemove, extraList, nullptr);
    }

    ResetActor3DForWornArmor(a_actor);
    if (auto runtimeIt = a_state.runtimeArmors.find(a_state.equippedOriginalFormID);
        runtimeIt != a_state.runtimeArmors.end()) {
        if (const auto handle = EventBindings::GetHandle(a_state.equippedOriginalFormID, equippedArmor->GetFormID())) {
            runtimeIt->second.eventTargetHandle = *handle;
        }
    }
    EventBindings::RemoveForClone(equippedArmor->GetFormID());

    a_state.equippedOriginalFormID = 0;
    a_state.equippedArmor = nullptr;
    a_state.equippedExtraList = nullptr;
    ClearEquippedSelection(a_state);
    a_state.equippedArmorRestoredFromSave = false;
}
}
