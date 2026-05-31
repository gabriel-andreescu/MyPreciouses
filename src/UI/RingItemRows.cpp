#include "UI/RingItemRows.h"

#include "Equipment/AssignmentStore.h"
#include "Inventory.h"
#include "Localization.h"
#include "SourceModelFootprints.h"
#include "UI/Scaleform.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <vector>

namespace UI::RingItemRows {
namespace {
    constexpr auto kSkyUIEquipStateNone = 0;
    constexpr auto kSkyUIEquipStateLeft = 2;
    constexpr auto kSkyUIEquipStateRight = 3;
    constexpr auto kSkyUIEquipStateBoth = 4;
    constexpr auto kScaleformItemFormID = "formId";
    constexpr auto kScaleformItemText = "text";
    constexpr auto kScaleformEquipState = "equipState";
    constexpr auto kScaleformSelectionKind = "lhrsSelectionKind";
    constexpr auto kScaleformCustomEnchantmentID = "lhrsCustomEnchantmentID";
    constexpr auto kScaleformCustomCharge = "lhrsCustomCharge";
    constexpr auto kScaleformCustomRemoveOnUnequip = "lhrsCustomRemoveOnUnequip";
    constexpr auto kScaleformCustomDisplayName = "lhrsCustomDisplayName";
    constexpr auto kScaleformCustomHasUniqueID = "lhrsCustomHasUniqueID";
    constexpr auto kScaleformCustomUniqueBaseID = "lhrsCustomUniqueBaseID";
    constexpr auto kScaleformCustomUniqueID = "lhrsCustomUniqueID";
    constexpr auto kScaleformCustomBlockReason = "lhrsCustomBlockReason";
    constexpr auto kScaleformVanillaRingSlotEquipped = "lhrsVanillaRingSlotEquipped";
    constexpr auto kScaleformRingBaseText = "lhrsBaseText";
    constexpr auto kRingRowLeftHandShortKey = "$LHRS_Inventory_LeftHandShort";
    constexpr auto kRingRowRightHandShortKey = "$LHRS_Inventory_RightHandShort";

    struct RingRowPresentation {
        RE::TESObjectARMO& ring;
        Core::ItemSource source;
        bool vanillaRingSlotEquipped {false};
        Inventory::EntryCustomFailure customFailure {Inventory::EntryCustomFailure::kNone};
    };

    [[nodiscard]] std::string GetRowHandLabel(const Core::Hand a_hand) {
        switch (a_hand) {
            case Core::Hand::kLeft:  return Localization::Translate(kRingRowLeftHandShortKey, "L");
            case Core::Hand::kRight: return Localization::Translate(kRingRowRightHandShortKey, "R");
        }

        return "?";
    }

    [[nodiscard]] bool IsVirtuallyEquippedOnHand(
        const Core::ActorKey a_actor,
        const Core::ItemSource& a_source,
        const Core::Hand a_hand
    ) {
        return std::ranges::any_of(Core::kVirtualTargets, [&](const auto target) {
            return target.hand == a_hand && Equipment::AssignmentStore::Get(a_actor, target).source.Matches(a_source);
        });
    }

    [[nodiscard]] int GetRingEquipState(
        const Core::ActorKey a_actor,
        const Core::ItemSource& a_source,
        const bool a_vanillaRingSlotEquipped
    ) {
        const auto leftEquipped = IsVirtuallyEquippedOnHand(a_actor, a_source, Core::Hand::kLeft);
        const auto rightEquipped = a_vanillaRingSlotEquipped
                                   || IsVirtuallyEquippedOnHand(a_actor, a_source, Core::Hand::kRight);

        if (leftEquipped && rightEquipped) {
            return kSkyUIEquipStateBoth;
        }

        if (leftEquipped) {
            return kSkyUIEquipStateLeft;
        }

        if (rightEquipped) {
            return kSkyUIEquipStateRight;
        }

        return kSkyUIEquipStateNone;
    }

    [[nodiscard]] std::vector<Core::Target> CollectRingRowTargets(
        const RE::TESObjectARMO& a_ring,
        const Core::ActorKey a_actor,
        const Core::ItemSource& a_source,
        const bool a_vanillaRingSlotEquipped
    ) {
        Core::TargetMask targetMask;
        auto addOccupiedTargets = [&targetMask](const Core::TargetMask& a_occupiedTargets) {
            for (const auto target : Core::kAllTargets) {
                if (a_occupiedTargets.Contains(target)) {
                    targetMask.Add(target);
                }
            }
        };

        if (a_vanillaRingSlotEquipped) {
            addOccupiedTargets(SourceModelFootprints::GetProjectedTargets(a_ring, Core::kVanillaRingSlotTarget));
        }

        for (const auto target : Core::kVirtualTargets) {
            if (Equipment::AssignmentStore::Get(a_actor, target).source.Matches(a_source)) {
                addOccupiedTargets(SourceModelFootprints::GetProjectedTargets(a_ring, target));
            }
        }

        std::vector<Core::Target> targets;
        targets.reserve(Core::kAllTargets.size());
        for (const auto target : Core::kAllTargets) {
            if (targetMask.Contains(target)) {
                targets.push_back(target);
            }
        }

        return targets;
    }

    [[nodiscard]] bool HasNonIndexTarget(const std::vector<Core::Target>& a_targets) {
        return std::ranges::any_of(a_targets, [](const auto target) {
            return target.finger != Core::Finger::kIndex;
        });
    }

    [[nodiscard]] bool ShouldShowRingFingerSuffix(const std::vector<Core::Target>& a_targets) {
        return a_targets.size() > 1 || HasNonIndexTarget(a_targets);
    }

    [[nodiscard]] std::string FormatRingRowTargetLabel(const Core::Target a_target) {
        auto label = GetRowHandLabel(a_target.hand);
        label.reserve(8);
        label.push_back(' ');
        label.append(Localization::TranslateFingerLabel(a_target.finger));
        return label;
    }

    [[nodiscard]] std::optional<std::string> GetRingRowBaseText(RE::GFxValue& a_entryObject) {
        if (auto baseText = Scaleform::ReadStringMember(a_entryObject, kScaleformRingBaseText)) {
            return baseText;
        }

        auto currentText = Scaleform::ReadStringMember(a_entryObject, kScaleformItemText);
        if (!currentText) {
            return std::nullopt;
        }

        RE::GFxValue baseTextValue;
        baseTextValue.SetString(*currentText);
        a_entryObject.SetMember(kScaleformRingBaseText, baseTextValue);
        return currentText;
    }

    [[nodiscard]] std::string FormatRingFingerSuffix(const std::vector<Core::Target>& a_targets) {
        std::string suffix {" ("};

        for (std::size_t index = 0; index < a_targets.size(); ++index) {
            if (index > 0) {
                suffix.append(", ");
            }
            suffix.append(FormatRingRowTargetLabel(a_targets[index]));
        }

        suffix.push_back(')');
        return suffix;
    }

    [[nodiscard]] bool UpdateRingRowLabel(
        RE::GFxValue& a_entryObject,
        const Core::ActorKey a_actor,
        const Core::ItemSource& a_source,
        const bool a_vanillaRingSlotEquipped
    ) {
        const auto baseText = GetRingRowBaseText(a_entryObject);
        if (!baseText) {
            return false;
        }

        auto displayText = *baseText;
        auto* ring = Inventory::AsRing(RE::TESForm::LookupByID(a_source.sourceFormID));
        const auto targets = ring ? CollectRingRowTargets(*ring, a_actor, a_source, a_vanillaRingSlotEquipped)
                                  : std::vector<Core::Target> {};
        if (ShouldShowRingFingerSuffix(targets)) {
            displayText.append(FormatRingFingerSuffix(targets));
        }

        if (const auto currentText = Scaleform::ReadStringMember(a_entryObject, kScaleformItemText);
            currentText && displayText == *currentText) {
            return false;
        }

        RE::GFxValue displayTextValue;
        displayTextValue.SetString(displayText);
        a_entryObject.SetMember(kScaleformItemText, displayTextValue);
        return true;
    }

    [[nodiscard]] Core::ItemSource ReadStampedItemSource(
        const RE::GFxValue& a_object,
        const RE::FormID a_sourceFormID
    ) {
        const auto selectionKind = static_cast<Core::ItemSourceKind>(
            Scaleform::ReadIntMember(a_object, kScaleformSelectionKind)
                .value_or(static_cast<int>(std::to_underlying(Core::ItemSourceKind::kNone)))
        );

        if (selectionKind != Core::ItemSourceKind::kCustomEnchantment) {
            return Core::ItemSource {
                .kind = selectionKind,
                .sourceFormID = a_sourceFormID,
            };
        }

        const auto enchantmentID = Scaleform::ReadUInt32Member(a_object, kScaleformCustomEnchantmentID);
        const auto charge = Scaleform::ReadUInt16Member(a_object, kScaleformCustomCharge);
        const auto removeOnUnequip = Scaleform::ReadBoolMember(a_object, kScaleformCustomRemoveOnUnequip);
        if (!enchantmentID || !charge || !removeOnUnequip) {
            return {};
        }

        std::optional<Core::ExtraUniqueIDKey> extraUniqueID;
        if (const auto hasUniqueID = Scaleform::ReadBoolMember(a_object, kScaleformCustomHasUniqueID);
            hasUniqueID.value_or(false)) {
            const auto uniqueBaseID = Scaleform::ReadUInt32Member(a_object, kScaleformCustomUniqueBaseID);
            const auto uniqueID = Scaleform::ReadUInt16Member(a_object, kScaleformCustomUniqueID);
            if (!uniqueBaseID || !uniqueID) {
                return {};
            }

            const auto uniqueIDKey = Core::ExtraUniqueIDKey {
                .baseID = *uniqueBaseID,
                .uniqueID = *uniqueID,
            };
            if (!uniqueIDKey.IsValid()) {
                return {};
            }

            extraUniqueID = uniqueIDKey;
        }

        return Core::ItemSource {
            .kind = Core::ItemSourceKind::kCustomEnchantment,
            .sourceFormID = a_sourceFormID,
            .customEnchantment = Core::CustomEnchantmentSignature {
                .enchantmentFormID = *enchantmentID,
                .charge = *charge,
                .removeOnUnequip = *removeOnUnequip,
                .playerDisplayName = Scaleform::ReadStringMember(a_object, kScaleformCustomDisplayName).value_or(
                    std::string {}
                ),
            },
            .extraUniqueID = extraUniqueID,
        };
    }

    void StampRingSource(
        RE::GFxValue& a_object,
        const Core::ItemSource& a_source,
        const Inventory::EntryCustomFailure a_failure
    ) {
        const auto customSource = a_source.IsCustomEnchantment();
        a_object.SetMember(kScaleformSelectionKind, std::to_underlying(a_source.kind));
        a_object.SetMember(
            kScaleformCustomEnchantmentID,
            customSource ? a_source.customEnchantment.enchantmentFormID : 0
        );
        a_object.SetMember(kScaleformCustomCharge, customSource ? a_source.customEnchantment.charge : 0);
        a_object.SetMember(kScaleformCustomRemoveOnUnequip, customSource && a_source.customEnchantment.removeOnUnequip);
        a_object.SetMember(kScaleformCustomHasUniqueID, a_source.extraUniqueID.has_value());
        a_object.SetMember(kScaleformCustomUniqueBaseID, a_source.extraUniqueID ? a_source.extraUniqueID->baseID : 0);
        a_object.SetMember(kScaleformCustomUniqueID, a_source.extraUniqueID ? a_source.extraUniqueID->uniqueID : 0);

        RE::GFxValue displayName;
        displayName.SetString(
            customSource ? std::string_view {a_source.customEnchantment.playerDisplayName} : std::string_view {}
        );
        a_object.SetMember(kScaleformCustomDisplayName, displayName);
        a_object.SetMember(kScaleformCustomBlockReason, std::to_underlying(a_failure));
    }

    void StampEquipState(RE::GFxValue& a_object, const int a_equipState, const bool a_vanillaRingSlotEquipped) {
        a_object.SetMember(kScaleformVanillaRingSlotEquipped, a_vanillaRingSlotEquipped);
        a_object.SetMember("equipState", a_equipState);
        a_object.SetMember("isEquipped", a_equipState > kSkyUIEquipStateNone);
    }

    [[nodiscard]] std::optional<RingRowPresentation> ResolveRingRowPresentation(
        RE::InventoryEntryData& a_entry,
        const Core::ActorKey a_actor
    ) {
        auto* actor = Core::ResolveActor(a_actor);
        if (!actor) {
            return std::nullopt;
        }

        auto source = Inventory::ResolveEntryRingSource(*actor, a_entry);
        if (!source) {
            return std::nullopt;
        }

        return RingRowPresentation {
            .ring = *source->ring,
            .source = source->source,
            .vanillaRingSlotEquipped = source->vanillaRingSlotEquipped,
            .customFailure = source->customFailure,
        };
    }

    [[nodiscard]] RowStampResult StampRingEntry(
        RE::GFxValue& a_object,
        const RingRowPresentation& a_presentation,
        const Core::ActorKey a_actor,
        const bool a_updateRowLabel
    ) {
        StampRingSource(a_object, a_presentation.source, a_presentation.customFailure);

        const auto equipState = GetRingEquipState(
            a_actor,
            a_presentation.source,
            a_presentation.vanillaRingSlotEquipped
        );
        const auto previousEquipState = Scaleform::ReadIntMember(a_object, kScaleformEquipState)
                                            .value_or(kSkyUIEquipStateNone);
        StampEquipState(a_object, equipState, a_presentation.vanillaRingSlotEquipped);

        const auto textChanged = a_updateRowLabel
                                 && UpdateRingRowLabel(
                                     a_object,
                                     a_actor,
                                     a_presentation.source,
                                     a_presentation.vanillaRingSlotEquipped
                                 );

        return previousEquipState != equipState || textChanged ? RowStampResult::kChanged : RowStampResult::kUnchanged;
    }
}

std::string GetRingDisplayName(const RE::TESObjectARMO& a_ring) {
    const auto* name = a_ring.GetName();
    return name && name[0] != '\0' ? name : kEmptyRingDisplayName;
}

RowStampResult StampRingEntry(
    RE::GFxValue& a_object,
    RE::InventoryEntryData& a_entry,
    const Core::ActorKey a_actor,
    const bool a_updateRowLabel
) {
    const auto presentation = ResolveRingRowPresentation(a_entry, a_actor);
    if (!presentation) {
        return RowStampResult::kIgnored;
    }

    return StampRingEntry(a_object, *presentation, a_actor, a_updateRowLabel);
}

RowStampResult RefreshStampedRingEntry(RE::GFxValue& a_entryObject, const Core::ActorKey a_actor) {
    const auto formID = Scaleform::ReadUInt32Member(a_entryObject, kScaleformItemFormID);
    if (!formID) {
        return RowStampResult::kIgnored;
    }

    auto* ring = Inventory::AsRing(RE::TESForm::LookupByID<RE::TESObjectARMO>(*formID));
    if (!ring) {
        return RowStampResult::kIgnored;
    }

    const auto previousEquipState = Scaleform::ReadIntMember(a_entryObject, kScaleformEquipState)
                                        .value_or(kSkyUIEquipStateNone);
    const auto previousVanillaRingSlotState = previousEquipState
                                              == kSkyUIEquipStateRight
                                              || previousEquipState
                                              == kSkyUIEquipStateBoth;
    const auto storedVanillaRingSlotState = Scaleform::ReadBoolMember(a_entryObject, kScaleformVanillaRingSlotEquipped);
    const auto source = ReadStampedItemSource(a_entryObject, *formID);
    const auto vanillaRingSlotEquipped = storedVanillaRingSlotState.value_or(previousVanillaRingSlotState);
    const auto equipState = GetRingEquipState(a_actor, source, vanillaRingSlotEquipped);
    const auto textChanged = UpdateRingRowLabel(a_entryObject, a_actor, source, vanillaRingSlotEquipped);

    StampEquipState(a_entryObject, equipState, vanillaRingSlotEquipped);
    return previousEquipState != equipState || textChanged ? RowStampResult::kChanged : RowStampResult::kUnchanged;
}

bool CanUseRingEquipHint(const RE::GFxValue& a_entryObject) {
    if (!Scaleform::CanReadMembers(a_entryObject)) {
        return false;
    }

    const auto formID = Scaleform::ReadUInt32Member(a_entryObject, kScaleformItemFormID).value_or(0);
    const auto source = ReadStampedItemSource(a_entryObject, formID);
    if (!source.IsAssigned()) {
        return false;
    }

    return static_cast<Inventory::EntryCustomFailure>(
               Scaleform::ReadIntMember(a_entryObject, kScaleformCustomBlockReason)
                   .value_or(static_cast<int>(std::to_underlying(Inventory::EntryCustomFailure::kNone)))
           )
           == Inventory::EntryCustomFailure::kNone;
}

bool CanShowFingerSelectHint(const RE::GFxValue& a_entryObject) {
    if (!CanUseRingEquipHint(a_entryObject)) {
        return false;
    }

    const auto formID = Scaleform::ReadUInt32Member(a_entryObject, kScaleformItemFormID);
    auto* ring = formID ? Inventory::AsRing(RE::TESForm::LookupByID<RE::TESObjectARMO>(*formID)) : nullptr;
    return ring && !SourceModelFootprints::GetSourceFingerMask(*ring).IsMultiFinger();
}
}
