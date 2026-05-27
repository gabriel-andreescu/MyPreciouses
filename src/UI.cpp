#include "UI.h"

#include "FingerSelectMenu.h"
#include "Inventory.h"
#include "RingFootprints.h"
#include "RingSounds.h"
#include "Selection.h"
#include "Settings.h"
#include "VirtualRings.h"

#include <RE/B/BSPCGamepadDeviceDelegate.h>
#include <RE/B/BSPCGamepadDeviceHandler.h>
#include <RE/B/BSWin32KeyboardDevice.h>
#include <RE/G/GFxFunctionHandler.h>
#include <RE/S/SendHUDMessage.h>
#include <RE/S/SendUIMessage.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace UI {
namespace {
    constexpr auto kSkyUIEquipStateNone = 0;
    constexpr auto kSkyUIEquipStateLeft = 2;
    constexpr auto kSkyUIEquipStateRight = 3;
    constexpr auto kSkyUIEquipStateBoth = 4;
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
    constexpr auto kScaleformInventoryBaseText = "lhrsBaseText";
    constexpr auto kScaleformInventoryEquipHintPatched = "lhrsInventoryEquipHintPatched";
    constexpr auto kScaleformInventoryFingerSelectHintPatched = "lhrsInventoryFingerSelectHintPatched";
    constexpr auto kInventoryMenuClipPath = "_root.Menu_mc";
    constexpr auto kSkyUISelectedEntryPath = "_root.Menu_mc.inventoryLists.itemList.selectedEntry";
    constexpr auto kSkyUIUpdateBottomBarPath = "_root.Menu_mc.updateBottomBar";
    constexpr auto kSkyUINavPanelAddButtonPath = "_root.Menu_mc.navPanel.addButton";
    constexpr auto kVanillaSelectedEntryPath = "_root.Menu_mc.InventoryLists_mc.ItemsList.selectedEntry";
    constexpr auto kVanillaUpdateBottomBarPath = "_root.Menu_mc.UpdateBottomBarButtons";
    constexpr auto kInventoryInvalidateListDataPath = "_root.Menu_mc.inventoryLists.InvalidateListData";
    constexpr auto kSkyUIGameplayContext = 0;
    constexpr auto kSkyUIItemMenuContext = 3;
    constexpr auto kSkyUIShiftKeyCode = 42;
    constexpr auto kFingerSelectTitle = "ASSIGN RING";
    constexpr auto kFingerSelectHintLabel = "Finger";
    constexpr auto kEmptyRingLabel = "-";
    constexpr auto kEquipActionLabel = "Equip";
    constexpr auto kUnequipActionLabel = "Unequip";
    constexpr auto kReplaceActionLabel = "Replace";

    struct RowSelection {
        Selection::Kind kind {Selection::Kind::kNone};
        std::optional<Inventory::CustomEnchantmentKey> customKey;
        std::optional<Inventory::ExtraListIdentity> customIdentity;
    };

    struct RingSelectionData {
        RE::TESObjectARMO* ring {nullptr};
        std::optional<Inventory::CustomEnchantmentKey> customKey;
        std::optional<Inventory::ExtraListIdentity> customIdentity;
        RE::ExtraDataList* sourceExtraList {nullptr};
        bool blocked {false};
    };

    struct StoredRingSelection {
        RE::FormID sourceFormID {0};
        std::optional<Inventory::CustomEnchantmentKey> customKey;
        std::optional<Inventory::ExtraListIdentity> customIdentity;
        SelectionOrigin origin {SelectionOrigin::kInventoryMenu};
    };

    struct RingRowState {
        RE::TESObjectARMO& ring;
        RowSelection selection;
        bool vanillaRingSlotEquipped {false};
        Inventory::EntryCustomFailure customFailure {Inventory::EntryCustomFailure::kNone};
    };

    struct FingerSelectTrigger {
        bool requested {false};
        RE::INPUT_DEVICE inputDevice {RE::INPUT_DEVICE::kKeyboard};
    };

    [[nodiscard]] RE::InventoryMenu* GetInventoryMenu() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui || !ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME)) {
            return nullptr;
        }

        auto inventoryMenu = ui->GetMenu<RE::InventoryMenu>();
        return inventoryMenu.get();
    }

    [[nodiscard]] bool IsInventoryMenuView(const RE::GFxMovieView* a_view) {
        if (!a_view) {
            return false;
        }

        auto* inventoryMenu = GetInventoryMenu();
        return inventoryMenu && inventoryMenu->uiMovie.get() == a_view;
    }

    [[nodiscard]] RE::FavoritesMenu* GetFavoritesMenu() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui || !ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME)) {
            return nullptr;
        }

        auto favoritesMenu = ui->GetMenu<RE::FavoritesMenu>();
        return favoritesMenu.get();
    }

    [[nodiscard]] bool GetFavoritesItemList(RE::FavoritesMenu& a_menu, RE::GFxValue& a_itemList) {
        auto& root = a_menu.GetRuntimeData().root;
        if (root.GetMember("ItemList", std::addressof(a_itemList))
            && (a_itemList.IsObject() || a_itemList.IsDisplayObject())) {
            return true;
        }

        return root.GetMember("itemList", std::addressof(a_itemList))
               && (a_itemList.IsObject() || a_itemList.IsDisplayObject());
    }

    [[nodiscard]] bool MatchesRowSelection(
        const Selection::State& a_selection,
        const RE::TESObjectARMO& a_ring,
        const RowSelection& a_rowSelection
    ) {
        const auto formID = a_ring.GetFormID();
        if (a_selection.sourceFormID == 0 || a_selection.sourceFormID != formID) {
            return false;
        }

        if (a_selection.MatchesForm(formID)) {
            return a_rowSelection.kind == Selection::Kind::kFormOnly;
        }

        return a_rowSelection.kind
               == Selection::Kind::kCustomEnchantment
               && a_rowSelection.customKey
               && a_selection
                      .MatchesCustomEnchantment(formID, *a_rowSelection.customKey, a_rowSelection.customIdentity);
    }

    [[nodiscard]] bool IsVirtuallyEquippedOnHand(
        const RE::TESObjectARMO& a_ring,
        const RowSelection& a_rowSelection,
        const RingHand a_hand
    ) {
        return std::ranges::any_of(kVirtualRingTargets, [&](const auto target) {
            return target.hand == a_hand && MatchesRowSelection(Selection::Get(target), a_ring, a_rowSelection);
        });
    }

    [[nodiscard]] int GetRingEquipState(
        const RE::TESObjectARMO& a_ring,
        const RowSelection& a_rowSelection,
        const bool a_vanillaRingSlotEquipped
    ) {
        const auto leftEquipped = IsVirtuallyEquippedOnHand(a_ring, a_rowSelection, RingHand::kLeft);
        const auto rightEquipped = a_vanillaRingSlotEquipped
                                   || IsVirtuallyEquippedOnHand(a_ring, a_rowSelection, RingHand::kRight);

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

    [[nodiscard]] std::vector<RingTarget> CollectInventoryRowTargets(
        const RE::TESObjectARMO& a_ring,
        const RowSelection& a_rowSelection,
        const bool a_vanillaRingSlotEquipped
    ) {
        std::vector<RingTarget> targets;
        targets.reserve(kRingTargets.size());

        for (const auto target : kRingTargets) {
            if (target == kVanillaRingTarget) {
                if (a_vanillaRingSlotEquipped) {
                    targets.push_back(target);
                }
                continue;
            }

            if (MatchesRowSelection(Selection::Get(target), a_ring, a_rowSelection)) {
                targets.push_back(target);
            }
        }

        return targets;
    }

    [[nodiscard]] bool HasNonIndexTarget(const std::vector<RingTarget>& a_targets) {
        return std::ranges::any_of(a_targets, [](const auto target) {
            return target.finger != RingFinger::kIndex;
        });
    }

    [[nodiscard]] std::string FormatInventoryTargetLabel(const RingTarget a_target) {
        std::string label;
        label.reserve(8);
        label.push_back(TargetSideCode(a_target));
        label.push_back(' ');
        label.append(FingerLabel(a_target.finger));
        return label;
    }

    [[nodiscard]] std::optional<std::string> GetInventoryRowBaseText(RE::GFxValue& a_entryObject) {
        RE::GFxValue baseText;
        if (a_entryObject.GetMember(kScaleformInventoryBaseText, std::addressof(baseText)) && baseText.IsString()) {
            if (const auto* text = baseText.GetString()) {
                return std::string {text};
            }
        }

        RE::GFxValue currentText;
        if (!a_entryObject.GetMember("text", std::addressof(currentText)) || !currentText.IsString()) {
            return std::nullopt;
        }

        const auto* currentTextValue = currentText.GetString();
        if (!currentTextValue) {
            return std::nullopt;
        }

        std::string text {currentTextValue};
        RE::GFxValue baseTextValue;
        baseTextValue.SetString(text);
        a_entryObject.SetMember(kScaleformInventoryBaseText, baseTextValue);
        return text;
    }

    [[nodiscard]] std::string FormatInventoryFingerSuffix(const std::vector<RingTarget>& a_targets) {
        std::string suffix {" ("};

        for (std::size_t index = 0; index < a_targets.size(); ++index) {
            if (index > 0) {
                suffix.append(", ");
            }
            suffix.append(FormatInventoryTargetLabel(a_targets[index]));
        }

        suffix.push_back(')');
        return suffix;
    }

    [[nodiscard]] bool UpdateInventoryRowText(
        RE::GFxValue& a_entryObject,
        const RE::TESObjectARMO& a_ring,
        const RowSelection& a_rowSelection,
        const bool a_vanillaRingSlotEquipped
    ) {
        const auto baseText = GetInventoryRowBaseText(a_entryObject);
        if (!baseText) {
            return false;
        }

        auto displayText = *baseText;
        const auto targets = CollectInventoryRowTargets(a_ring, a_rowSelection, a_vanillaRingSlotEquipped);
        if (HasNonIndexTarget(targets)) {
            displayText.append(FormatInventoryFingerSuffix(targets));
        }

        RE::GFxValue currentText;
        if (a_entryObject.GetMember("text", std::addressof(currentText)) && currentText.IsString()) {
            const auto* currentTextValue = currentText.GetString();
            if (currentTextValue && displayText == currentTextValue) {
                return false;
            }
        }

        RE::GFxValue displayTextValue;
        displayTextValue.SetString(displayText);
        a_entryObject.SetMember("text", displayTextValue);
        return true;
    }

    [[nodiscard]] bool IsFormRowInVanillaRingSlot(RE::InventoryEntryData& a_entry) {
        if (!a_entry.extraLists) {
            return a_entry.IsWorn(false);
        }

        return std::ranges::any_of(*a_entry.extraLists, [](const auto* extraList) {
            return Inventory::IsRightWorn(extraList) && !Inventory::HasCustomEnchantment(extraList);
        });
    }

    [[nodiscard]] std::optional<RE::FormID> GetScaleformFormID(const RE::GFxValue& a_object) {
        if (!a_object.IsObject() && !a_object.IsDisplayObject()) {
            return std::nullopt;
        }

        RE::GFxValue formID;
        if (!a_object.GetMember("formId", std::addressof(formID)) || !formID.IsNumber()) {
            return std::nullopt;
        }

        return static_cast<RE::FormID>(formID.GetNumber());
    }

    [[nodiscard]] std::optional<std::uint32_t> GetScaleformIndex(const RE::GFxValue& a_object) {
        RE::GFxValue index;
        if (!a_object.GetMember("index", std::addressof(index)) || !index.IsNumber()) {
            return std::nullopt;
        }

        const auto value = static_cast<std::int32_t>(index.GetNumber());
        if (value < 0) {
            return std::nullopt;
        }

        return static_cast<std::uint32_t>(value);
    }

    [[nodiscard]] int GetScaleformEquipState(const RE::GFxValue& a_object) {
        RE::GFxValue equipState;
        if (!a_object.GetMember("equipState", std::addressof(equipState)) || !equipState.IsNumber()) {
            return kSkyUIEquipStateNone;
        }

        return static_cast<int>(equipState.GetNumber());
    }

    [[nodiscard]] int GetScaleformIntMember(const RE::GFxValue& a_object, const char* a_member, const int a_default) {
        RE::GFxValue value;
        if (!a_object.GetMember(a_member, std::addressof(value)) || !value.IsNumber()) {
            return a_default;
        }

        return static_cast<int>(value.GetNumber());
    }

    [[nodiscard]] std::optional<bool> GetScaleformBoolMember(const RE::GFxValue& a_object, const char* a_member) {
        RE::GFxValue value;
        if (!a_object.GetMember(a_member, std::addressof(value)) || !value.IsBool()) {
            return std::nullopt;
        }

        return value.GetBool();
    }

    [[nodiscard]] std::string GetScaleformStringMember(const RE::GFxValue& a_object, const char* a_member) {
        RE::GFxValue value;
        if (!a_object.GetMember(a_member, std::addressof(value)) || !value.IsString()) {
            return {};
        }

        const auto* string = value.GetString();
        return string ? string : "";
    }

    [[nodiscard]] RowSelection GetScaleformRowSelection(const RE::GFxValue& a_object) {
        const auto selectionKind = static_cast<Selection::Kind>(GetScaleformIntMember(
            a_object,
            kScaleformSelectionKind,
            static_cast<int>(std::to_underlying(Selection::Kind::kNone))
        ));

        if (selectionKind != Selection::Kind::kCustomEnchantment) {
            return RowSelection {
                .kind = selectionKind,
            };
        }

        RE::GFxValue enchantmentID;
        RE::GFxValue charge;
        RE::GFxValue removeOnUnequip;
        if (!a_object.GetMember(kScaleformCustomEnchantmentID, std::addressof(enchantmentID))
            || !enchantmentID.IsNumber()) {
            return RowSelection {
                .kind = Selection::Kind::kNone,
            };
        }

        if (!a_object.GetMember(kScaleformCustomCharge, std::addressof(charge)) || !charge.IsNumber()) {
            return RowSelection {
                .kind = Selection::Kind::kNone,
            };
        }

        if (!a_object.GetMember(kScaleformCustomRemoveOnUnequip, std::addressof(removeOnUnequip))
            || !removeOnUnequip.IsBool()) {
            return RowSelection {
                .kind = Selection::Kind::kNone,
            };
        }

        std::optional<Inventory::ExtraListIdentity> customIdentity;
        if (const auto hasUniqueID = GetScaleformBoolMember(a_object, kScaleformCustomHasUniqueID);
            hasUniqueID.value_or(false)) {
            RE::GFxValue uniqueBaseID;
            RE::GFxValue uniqueID;
            if (!a_object.GetMember(kScaleformCustomUniqueBaseID, std::addressof(uniqueBaseID))
                || !uniqueBaseID.IsNumber()
                || !a_object.GetMember(kScaleformCustomUniqueID, std::addressof(uniqueID))
                || !uniqueID.IsNumber()) {
                return RowSelection {
                    .kind = Selection::Kind::kNone,
                };
            }

            const auto identity = Inventory::ExtraListIdentity {
                .baseID = static_cast<RE::FormID>(uniqueBaseID.GetNumber()),
                .uniqueID = static_cast<std::uint16_t>(uniqueID.GetNumber()),
            };
            if (!identity.IsValid()) {
                return RowSelection {
                    .kind = Selection::Kind::kNone,
                };
            }

            customIdentity = identity;
        }

        return RowSelection {
            .kind = Selection::Kind::kCustomEnchantment,
            .customKey = Inventory::CustomEnchantmentKey {
                .enchantmentFormID = static_cast<RE::FormID>(enchantmentID.GetNumber()),
                .charge = static_cast<std::uint16_t>(charge.GetNumber()),
                .removeOnUnequip = removeOnUnequip.GetBool(),
                .playerDisplayName = GetScaleformStringMember(a_object, kScaleformCustomDisplayName),
            },
            .customIdentity = customIdentity,
        };
    }

    [[nodiscard]] bool IsScaleformObject(const RE::GFxValue& a_value) {
        return a_value.IsObject() || a_value.IsDisplayObject();
    }

    [[nodiscard]] bool IsInventoryRingEquipHintRow(const RE::GFxValue& a_entryObject) {
        if (!IsScaleformObject(a_entryObject)) {
            return false;
        }

        const auto selection = GetScaleformRowSelection(a_entryObject);
        if (selection.kind == Selection::Kind::kNone) {
            return false;
        }

        const auto customFailure = static_cast<Inventory::EntryCustomFailure>(GetScaleformIntMember(
            a_entryObject,
            kScaleformCustomBlockReason,
            static_cast<int>(std::to_underlying(Inventory::EntryCustomFailure::kNone))
        ));
        return customFailure == Inventory::EntryCustomFailure::kNone;
    }

    [[nodiscard]] std::optional<RE::GFxValue> GetInventorySelectedEntry(
        const RE::GFxMovie& a_movie,
        const char* a_path
    ) {
        RE::GFxValue selectedEntry;
        if (!a_movie.GetVariable(std::addressof(selectedEntry), a_path) || !IsScaleformObject(selectedEntry)) {
            return std::nullopt;
        }

        return selectedEntry;
    }

    [[nodiscard]] bool IsSelectedInventoryRingEquipHintRow(const RE::GFxMovie& a_movie, const char* a_path) {
        const auto selectedEntry = GetInventorySelectedEntry(a_movie, a_path);
        return selectedEntry && IsInventoryRingEquipHintRow(*selectedEntry);
    }

    [[nodiscard]] bool IsSelectedInventoryFingerSelectHintRow(const RE::GFxMovie& a_movie) {
        const auto selectedEntry = GetInventorySelectedEntry(a_movie, kSkyUISelectedEntryPath);
        if (!selectedEntry || !IsInventoryRingEquipHintRow(*selectedEntry)) {
            return false;
        }

        const auto formID = GetScaleformFormID(*selectedEntry);
        auto* ring = formID ? Inventory::AsRing(RE::TESForm::LookupByID<RE::TESObjectARMO>(*formID)) : nullptr;
        return ring && !RingFootprints::GetSourceRingFootprint(*ring).IsMultiFinger();
    }

    void SetSkyUIEquipControl(RE::GFxMovie& a_movie, RE::GFxValue& a_control, const char* a_name) {
        a_movie.CreateObject(std::addressof(a_control));

        RE::GFxValue name;
        name.SetString(a_name);
        a_control.SetMember("name", name);
        a_control.SetMember("context", kSkyUIItemMenuContext);
    }

    void SetSkyUIFingerSelectHintControl(
        RE::GFxMovie& a_movie,
        RE::GFxValue& a_control,
        const RE::GFxValue& a_inventoryMenu
    ) {
        a_movie.CreateObject(std::addressof(a_control));

        const auto platform = GetScaleformIntMember(a_inventoryMenu, "_platform", 0);
        if (platform == 0) {
            a_control.SetMember("keyCode", kSkyUIShiftKeyCode);
            return;
        }

        RE::GFxValue name;
        name.SetString("Sprint");
        a_control.SetMember("name", name);
        a_control.SetMember("context", kSkyUIGameplayContext);
    }

    void SetSkyUIRingEquipButtonData(RE::GFxMovie& a_movie, RE::GFxValue& a_result) {
        a_movie.CreateObject(std::addressof(a_result));

        RE::GFxValue text;
        text.SetString("$Equip");
        a_result.SetMember("text", text);

        RE::GFxValue controls;
        a_movie.CreateArray(std::addressof(controls));

        RE::GFxValue rightEquip;
        SetSkyUIEquipControl(a_movie, rightEquip, "RightEquip");
        controls.SetElement(0, rightEquip);

        RE::GFxValue leftEquip;
        SetSkyUIEquipControl(a_movie, leftEquip, "LeftEquip");
        controls.SetElement(1, leftEquip);

        a_result.SetMember("controls", controls);
    }

    void SetSkyUIFingerSelectHintButtonData(
        RE::GFxMovie& a_movie,
        RE::GFxValue& a_inventoryMenu,
        RE::GFxValue& a_result
    ) {
        a_movie.CreateObject(std::addressof(a_result));

        RE::GFxValue text;
        text.SetString(kFingerSelectHintLabel);
        a_result.SetMember("text", text);

        RE::GFxValue controls;
        SetSkyUIFingerSelectHintControl(a_movie, controls, a_inventoryMenu);
        a_result.SetMember("controls", controls);
    }

    void SetVanillaRingEquipButtonArt(RE::GFxValue& a_inventoryMenu) {
        RE::GFxValue bottomBar;
        if (!a_inventoryMenu.GetMember("BottomBar_mc", std::addressof(bottomBar)) || !IsScaleformObject(bottomBar)) {
            return;
        }

        RE::GFxValue equipButtonArt;
        if (!a_inventoryMenu.GetMember("EquipButtonArt", std::addressof(equipButtonArt))
            || !equipButtonArt.IsObject()) {
            return;
        }

        std::array<RE::GFxValue, 2> args;
        args[0] = equipButtonArt;
        args[1].SetNumber(0.0);
        static_cast<void>(bottomBar.Invoke("SetButtonArt", args));
    }

    class SkyUIEquipButtonDataHandler final : public RE::GFxFunctionHandler {
    public:
        explicit SkyUIEquipButtonDataHandler(const RE::GFxValue& a_originalFunction)
            : originalFunction_(a_originalFunction) {}

        void Call(Params& a_params) override {
            if (IsSelectedInventoryRingEquipHintRow(*a_params.movie, kSkyUISelectedEntryPath)) {
                SetSkyUIRingEquipButtonData(*a_params.movie, *a_params.retVal);
                return;
            }

            originalFunction_.Invoke(
                "call",
                a_params.retVal,
                a_params.argsWithThisRef,
                static_cast<std::size_t>(a_params.argCount) + 1
            );
        }

    private:
        RE::GFxValue originalFunction_;
    };

    class SkyUIAddButtonBeforeEquipHandler final : public RE::GFxFunctionHandler {
    public:
        SkyUIAddButtonBeforeEquipHandler(const RE::GFxValue& a_originalFunction, const RE::GFxValue& a_inventoryMenu)
            : originalFunction_(a_originalFunction)
            , inventoryMenu_(a_inventoryMenu) {}

        void Call(Params& a_params) override {
            if (!hintAdded_) {
                // SkyUI adds Equip first in updateBottomBar(true), then the remaining footer buttons.
                hintAdded_ = true;

                RE::GFxValue buttonData;
                SetSkyUIFingerSelectHintButtonData(*a_params.movie, inventoryMenu_, buttonData);

                std::array<RE::GFxValue, 2> args {*a_params.thisPtr, buttonData};
                RE::GFxValue result;
                static_cast<void>(originalFunction_.Invoke("call", std::addressof(result), args.data(), args.size()));
            }

            originalFunction_.Invoke(
                "call",
                a_params.retVal,
                a_params.argsWithThisRef,
                static_cast<std::size_t>(a_params.argCount) + 1
            );
        }

    private:
        RE::GFxValue originalFunction_;
        RE::GFxValue inventoryMenu_;
        bool hintAdded_ {false};
    };

    class SkyUIBottomBarUpdateHandler final : public RE::GFxFunctionHandler {
    public:
        explicit SkyUIBottomBarUpdateHandler(const RE::GFxValue& a_originalFunction)
            : originalFunction_(a_originalFunction) {}

        void Call(Params& a_params) override {
            const auto selected = a_params.argCount
                                  > 0
                                  && a_params.args
                                  && a_params.args[0].IsBool()
                                  && a_params.args[0].GetBool();

            RE::GFxValue navPanel;
            RE::GFxValue originalAddButton;
            RE::GPtr<SkyUIAddButtonBeforeEquipHandler> addButtonHandler;
            auto restoreAddButton = false;

            if (selected
                && !Settings::GetSingleton()->AlwaysChooseFinger()
                && IsSelectedInventoryFingerSelectHintRow(*a_params.movie)
                && a_params.thisPtr->GetMember("navPanel", std::addressof(navPanel))
                && IsScaleformObject(navPanel)
                && navPanel.GetMember("addButton", std::addressof(originalAddButton))
                && originalAddButton.IsObject()) {
                addButtonHandler = RE::make_gptr<SkyUIAddButtonBeforeEquipHandler>(
                    originalAddButton,
                    *a_params.thisPtr
                );

                RE::GFxValue function;
                a_params.movie->CreateFunction(std::addressof(function), addButtonHandler.get());
                restoreAddButton = navPanel.SetMember("addButton", function);
            }

            originalFunction_.Invoke(
                "call",
                a_params.retVal,
                a_params.argsWithThisRef,
                static_cast<std::size_t>(a_params.argCount) + 1
            );

            if (restoreAddButton) {
                navPanel.SetMember("addButton", originalAddButton);
            }
        }

    private:
        RE::GFxValue originalFunction_;
    };

    class VanillaBottomBarUpdateHandler final : public RE::GFxFunctionHandler {
    public:
        explicit VanillaBottomBarUpdateHandler(const RE::GFxValue& a_originalFunction)
            : originalFunction_(a_originalFunction) {}

        void Call(Params& a_params) override {
            originalFunction_.Invoke(
                "call",
                a_params.retVal,
                a_params.argsWithThisRef,
                static_cast<std::size_t>(a_params.argCount) + 1
            );

            if (IsSelectedInventoryRingEquipHintRow(*a_params.movie, kVanillaSelectedEntryPath)) {
                SetVanillaRingEquipButtonArt(*a_params.thisPtr);
            }
        }

    private:
        RE::GFxValue originalFunction_;
    };

    [[nodiscard]] bool InstallSkyUIInventoryEquipButtonHint(RE::GFxMovie& a_movie, RE::GFxValue& a_inventoryMenu) {
        if (GetScaleformBoolMember(a_inventoryMenu, kScaleformInventoryEquipHintPatched).value_or(false)) {
            return false;
        }

        if (!a_movie.IsAvailable(kSkyUIUpdateBottomBarPath)) {
            return false;
        }

        RE::GFxValue originalFunction;
        if (!a_inventoryMenu.GetMember("getEquipButtonData", std::addressof(originalFunction))
            || !originalFunction.IsObject()) {
            return false;
        }

        auto handler = RE::make_gptr<SkyUIEquipButtonDataHandler>(originalFunction);
        RE::GFxValue function;
        a_movie.CreateFunction(std::addressof(function), handler.get());
        if (!a_inventoryMenu.SetMember("getEquipButtonData", function)) {
            return false;
        }

        a_inventoryMenu.SetMember(kScaleformInventoryEquipHintPatched, true);
        return true;
    }

    [[nodiscard]] bool InstallSkyUIFingerSelectButtonHint(RE::GFxMovie& a_movie, RE::GFxValue& a_inventoryMenu) {
        if (GetScaleformBoolMember(a_inventoryMenu, kScaleformInventoryFingerSelectHintPatched).value_or(false)) {
            return false;
        }

        if (!a_movie.IsAvailable(kSkyUIUpdateBottomBarPath) || !a_movie.IsAvailable(kSkyUINavPanelAddButtonPath)) {
            return false;
        }

        RE::GFxValue originalFunction;
        if (!a_inventoryMenu.GetMember("updateBottomBar", std::addressof(originalFunction))
            || !originalFunction.IsObject()) {
            return false;
        }

        auto handler = RE::make_gptr<SkyUIBottomBarUpdateHandler>(originalFunction);
        RE::GFxValue function;
        a_movie.CreateFunction(std::addressof(function), handler.get());
        if (!a_inventoryMenu.SetMember("updateBottomBar", function)) {
            return false;
        }

        a_inventoryMenu.SetMember(kScaleformInventoryFingerSelectHintPatched, true);
        return true;
    }

    [[nodiscard]] bool InstallVanillaInventoryEquipButtonHint(RE::GFxMovie& a_movie, RE::GFxValue& a_inventoryMenu) {
        if (GetScaleformBoolMember(a_inventoryMenu, kScaleformInventoryEquipHintPatched).value_or(false)) {
            return false;
        }

        if (!a_movie.IsAvailable(kVanillaUpdateBottomBarPath)) {
            return false;
        }

        RE::GFxValue originalFunction;
        if (!a_inventoryMenu.GetMember("UpdateBottomBarButtons", std::addressof(originalFunction))
            || !originalFunction.IsObject()) {
            return false;
        }

        auto handler = RE::make_gptr<VanillaBottomBarUpdateHandler>(originalFunction);
        RE::GFxValue function;
        a_movie.CreateFunction(std::addressof(function), handler.get());
        if (!a_inventoryMenu.SetMember("UpdateBottomBarButtons", function)) {
            return false;
        }

        a_inventoryMenu.SetMember(kScaleformInventoryEquipHintPatched, true);
        return true;
    }

    [[nodiscard]] bool InstallInventoryButtonHints(RE::InventoryMenu& a_inventoryMenu) {
        auto* movie = a_inventoryMenu.uiMovie.get();
        if (!movie) {
            return false;
        }

        RE::GFxValue inventoryMenu;
        if (!movie->GetVariable(std::addressof(inventoryMenu), kInventoryMenuClipPath)
            || !IsScaleformObject(inventoryMenu)) {
            return false;
        }

        auto installed = InstallSkyUIInventoryEquipButtonHint(*movie, inventoryMenu);
        installed = InstallSkyUIFingerSelectButtonHint(*movie, inventoryMenu) || installed;
        installed = InstallVanillaInventoryEquipButtonHint(*movie, inventoryMenu) || installed;
        return installed;
    }

    void RefreshInventoryButtonHints(RE::InventoryMenu& a_inventoryMenu) {
        auto* movie = a_inventoryMenu.uiMovie.get();
        if (!movie) {
            return;
        }

        if (movie->IsAvailable(kSkyUIUpdateBottomBarPath)) {
            std::array<RE::GFxValue, 1> args;
            args[0].SetBoolean(GetInventorySelectedEntry(*movie, kSkyUISelectedEntryPath).has_value());
            static_cast<void>(
                movie->Invoke(kSkyUIUpdateBottomBarPath, nullptr, args.data(), static_cast<std::uint32_t>(args.size()))
            );
            return;
        }

        if (movie->IsAvailable(kVanillaUpdateBottomBarPath)
            && GetInventorySelectedEntry(*movie, kVanillaSelectedEntryPath).has_value()) {
            static_cast<void>(movie->Invoke(kVanillaUpdateBottomBarPath, nullptr, nullptr, 0));
        }
    }

    void StampScaleformSelection(
        RE::GFxValue& a_object,
        const Selection::Kind a_kind,
        const std::optional<Inventory::CustomEnchantmentKey>& a_customKey,
        const std::optional<Inventory::ExtraListIdentity>& a_customIdentity,
        const Inventory::EntryCustomFailure a_failure
    ) {
        a_object.SetMember(kScaleformSelectionKind, std::to_underlying(a_kind));
        a_object.SetMember(kScaleformCustomEnchantmentID, a_customKey ? a_customKey->enchantmentFormID : 0);
        a_object.SetMember(kScaleformCustomCharge, a_customKey ? a_customKey->charge : 0);
        a_object.SetMember(kScaleformCustomRemoveOnUnequip, a_customKey && a_customKey->removeOnUnequip);
        a_object.SetMember(kScaleformCustomHasUniqueID, a_customIdentity.has_value());
        a_object.SetMember(kScaleformCustomUniqueBaseID, a_customIdentity ? a_customIdentity->baseID : 0);
        a_object.SetMember(kScaleformCustomUniqueID, a_customIdentity ? a_customIdentity->uniqueID : 0);

        RE::GFxValue displayName;
        displayName.SetString(a_customKey ? std::string_view {a_customKey->playerDisplayName} : std::string_view {});
        a_object.SetMember(kScaleformCustomDisplayName, displayName);
        a_object.SetMember(kScaleformCustomBlockReason, std::to_underlying(a_failure));
    }

    [[nodiscard]] std::optional<Inventory::ExtraListIdentity> EnsureCustomSelectionIdentity(
        RE::TESObjectARMO& a_ring,
        Inventory::EntryCustomSelection& a_customSelection
    ) {
        if (!a_customSelection.HasCustomEnchantment() || !a_customSelection.extraList) {
            return std::nullopt;
        }

        if (a_customSelection.identity) {
            return a_customSelection.identity;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            logger::warn(
                "UI: custom selection identity skipped | form={:08X} | extraList={} | reason=noPlayer",
                a_ring.GetFormID(),
                static_cast<const void*>(a_customSelection.extraList)
            );
            return std::nullopt;
        }

        a_customSelection.identity = Inventory::EnsureExtraListIdentity(*player, a_ring, *a_customSelection.extraList);
        return a_customSelection.identity;
    }

    [[nodiscard]] std::optional<RingRowState> ResolveRingRowState(RE::InventoryEntryData& a_entry) {
        auto* ring = Inventory::AsRing(a_entry.GetObject());
        if (!ring) {
            return std::nullopt;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || Inventory::GetCount(*player, *ring) <= 0) {
            return std::nullopt;
        }

        auto customSelection = Inventory::ResolveEntryCustomSelection(a_entry);
        auto selectionKind = Selection::Kind::kFormOnly;
        std::optional<Inventory::CustomEnchantmentKey> customKey;
        std::optional<Inventory::ExtraListIdentity> customIdentity;
        auto vanillaRingSlotEquipped = IsFormRowInVanillaRingSlot(a_entry);

        if (customSelection.failure != Inventory::EntryCustomFailure::kNone) {
            selectionKind = Selection::Kind::kNone;
        } else if (customSelection.HasCustomEnchantment()) {
            customIdentity = EnsureCustomSelectionIdentity(*ring, customSelection);
            if (customIdentity) {
                selectionKind = Selection::Kind::kCustomEnchantment;
                customKey = customSelection.key;
                vanillaRingSlotEquipped = Inventory::IsRightWorn(customSelection.extraList);
            } else {
                selectionKind = Selection::Kind::kNone;
            }
        }

        return RingRowState {
            .ring = *ring,
            .selection = RowSelection {
                .kind = selectionKind,
                .customKey = customKey,
                .customIdentity = customIdentity,
            },
            .vanillaRingSlotEquipped = vanillaRingSlotEquipped,
            .customFailure = customSelection.failure,
        };
    }

    [[nodiscard]] bool StampEntryRingObject(RE::GFxValue& a_object, const RingRowState& a_rowState) {
        StampScaleformSelection(
            a_object,
            a_rowState.selection.kind,
            a_rowState.selection.customKey,
            a_rowState.selection.customIdentity,
            a_rowState.customFailure
        );

        const auto equipState = GetRingEquipState(
            a_rowState.ring,
            a_rowState.selection,
            a_rowState.vanillaRingSlotEquipped
        );
        const auto previousEquipState = GetScaleformEquipState(a_object);
        a_object.SetMember(kScaleformVanillaRingSlotEquipped, a_rowState.vanillaRingSlotEquipped);
        a_object.SetMember("equipState", equipState);
        a_object.SetMember("isEquipped", equipState > kSkyUIEquipStateNone);

        return previousEquipState != equipState;
    }

    [[nodiscard]] std::optional<bool> StampEntryRingObject(RE::GFxValue& a_object, RE::InventoryEntryData& a_entry) {
        const auto rowState = ResolveRingRowState(a_entry);
        if (!rowState) {
            return std::nullopt;
        }

        return StampEntryRingObject(a_object, *rowState);
    }

    [[nodiscard]] RE::InventoryEntryData* GetFavoritesEntryDataForRow(
        RE::FavoritesMenu& a_menu,
        const RE::GFxValue& a_entryObject
    ) {
        const auto favoriteIndex = GetScaleformIndex(a_entryObject);
        if (!favoriteIndex) {
            return nullptr;
        }

        auto& favorites = a_menu.GetRuntimeData().favorites;
        if (*favoriteIndex >= favorites.size()) {
            return nullptr;
        }

        auto* entry = favorites[*favoriteIndex].entryData;
        if (!entry || !entry->GetObject()) {
            return nullptr;
        }

        const auto formID = GetScaleformFormID(a_entryObject);
        if (formID && entry->GetObject()->GetFormID() != *formID) {
            return nullptr;
        }

        return entry;
    }

    [[nodiscard]] std::optional<bool> StampFavoritesEntryObject(
        RE::FavoritesMenu& a_menu,
        RE::GFxValue& a_entryObject
    ) {
        auto* entry = GetFavoritesEntryDataForRow(a_menu, a_entryObject);
        if (!entry) {
            return std::nullopt;
        }

        return StampEntryRingObject(a_entryObject, *entry);
    }

    void InventoryDataCallback(RE::GFxMovieView* a_view, RE::GFxValue* a_object, RE::InventoryEntryData* a_item) {
        if (!a_object || !a_item) {
            return;
        }

        const auto rowState = ResolveRingRowState(*a_item);
        if (!rowState) {
            return;
        }

        static_cast<void>(StampEntryRingObject(*a_object, *rowState));

        if (IsInventoryMenuView(a_view)) {
            static_cast<void>(UpdateInventoryRowText(
                *a_object,
                rowState->ring,
                rowState->selection,
                rowState->vanillaRingSlotEquipped
            ));
        }
    }

    [[nodiscard]] std::optional<bool> RestampInventoryRowFromStoredSelection(RE::GFxValue& a_entryObject) {
        const auto formID = GetScaleformFormID(a_entryObject);
        if (!formID) {
            return std::nullopt;
        }

        auto* ring = Inventory::AsRing(RE::TESForm::LookupByID<RE::TESObjectARMO>(*formID));
        if (!ring) {
            return std::nullopt;
        }

        const auto previousEquipState = GetScaleformEquipState(a_entryObject);
        const auto previousVanillaRingSlotState = previousEquipState
                                                  == kSkyUIEquipStateRight
                                                  || previousEquipState
                                                  == kSkyUIEquipStateBoth;
        const auto storedVanillaRingSlotState = GetScaleformBoolMember(
            a_entryObject,
            kScaleformVanillaRingSlotEquipped
        );
        const auto rowSelection = GetScaleformRowSelection(a_entryObject);
        const auto vanillaRingSlotEquipped = storedVanillaRingSlotState.value_or(previousVanillaRingSlotState);
        const auto equipState = GetRingEquipState(*ring, rowSelection, vanillaRingSlotEquipped);
        const auto textChanged = UpdateInventoryRowText(a_entryObject, *ring, rowSelection, vanillaRingSlotEquipped);

        a_entryObject.SetMember(kScaleformVanillaRingSlotEquipped, vanillaRingSlotEquipped);
        a_entryObject.SetMember("equipState", equipState);
        a_entryObject.SetMember("isEquipped", equipState > kSkyUIEquipStateNone);

        return previousEquipState != equipState || textChanged;
    }

    void InvalidateInventoryListData(RE::InventoryMenu& a_menu) {
        auto* movie = a_menu.uiMovie.get();
        if (!movie) {
            return;
        }

        if (!movie->IsAvailable(kInventoryInvalidateListDataPath)) {
            return;
        }

        if (!movie->Invoke(kInventoryInvalidateListDataPath, nullptr, nullptr, 0)) {
            return;
        }
    }

    void RestampInventoryRowsFromLiveState() {
        auto* inventoryMenu = GetInventoryMenu();
        if (!inventoryMenu) {
            return;
        }

        auto* itemList = inventoryMenu->GetRuntimeData().itemList;
        if (!itemList || !itemList->entryList.IsArray()) {
            return;
        }

        const auto buttonHintsInstalled = InstallInventoryButtonHints(*inventoryMenu);
        std::uint32_t changedEntryRows = 0;
        for (std::uint32_t index = 0; index < itemList->entryList.GetArraySize(); ++index) {
            RE::GFxValue entryObject;
            if (!itemList->entryList.GetElement(index, std::addressof(entryObject))
                || (!entryObject.IsObject() && !entryObject.IsDisplayObject())) {
                continue;
            }

            const auto changed = RestampInventoryRowFromStoredSelection(entryObject);
            if (!changed) {
                continue;
            }

            if (!*changed) {
                continue;
            }

            ++changedEntryRows;
            static_cast<void>(itemList->entryList.SetElement(index, entryObject));
        }

        if (changedEntryRows > 0) {
            InvalidateInventoryListData(*inventoryMenu);
        }

        if (buttonHintsInstalled) {
            RefreshInventoryButtonHints(*inventoryMenu);
        }
    }

    void DeselectInventoryItem(RE::InventoryMenu& a_menu) {
        auto* itemList = a_menu.GetRuntimeData().itemList;
        if (!itemList) {
            return;
        }

        static_cast<void>(itemList->root.SetMember("selectedIndex", RE::GFxValue {-1.0}));
    }

    [[nodiscard]] bool RedrawFavoritesRows(RE::GFxValue& a_itemList) {
        if (a_itemList.Invoke("InvalidateData")) {
            return true;
        }

        const auto requested = a_itemList.Invoke("requestInvalidate");
        return requested;
    }

    void RestampFavoritesRowsFromEntryData() {
        auto* favoritesMenu = GetFavoritesMenu();
        if (!favoritesMenu) {
            return;
        }

        RE::GFxValue itemList;
        if (!GetFavoritesItemList(*favoritesMenu, itemList)) {
            return;
        }

        RE::GFxValue entryList;
        if (!itemList.GetMember("entryList", std::addressof(entryList)) || !entryList.IsArray()) {
            return;
        }

        std::uint32_t ringRows = 0;
        std::uint32_t changedEntryRows = 0;
        std::uint32_t changedSelectedRows = 0;

        for (std::uint32_t index = 0; index < entryList.GetArraySize(); ++index) {
            RE::GFxValue entryObject;
            if (!entryList.GetElement(index, std::addressof(entryObject))
                || (!entryObject.IsObject() && !entryObject.IsDisplayObject())) {
                continue;
            }

            const auto changed = StampFavoritesEntryObject(*favoritesMenu, entryObject);
            if (!changed) {
                continue;
            }

            ++ringRows;
            changedEntryRows += *changed ? 1 : 0;
            static_cast<void>(entryList.SetElement(index, entryObject));
        }

        RE::GFxValue selectedEntry;
        if (itemList.GetMember("selectedEntry", std::addressof(selectedEntry))
            && (selectedEntry.IsObject() || selectedEntry.IsDisplayObject())) {
            if (const auto changed = StampFavoritesEntryObject(*favoritesMenu, selectedEntry)) {
                changedSelectedRows += *changed ? 1 : 0;
                static_cast<void>(itemList.SetMember("selectedEntry", selectedEntry));
            }
        }

        if (ringRows == 0) {
            return;
        }

        if (changedEntryRows == 0 && changedSelectedRows == 0) {
            return;
        }

        if (!RedrawFavoritesRows(itemList)) {
            return;
        }
    }

    void QueueInventoryRefresh() {
        stl::add_ui_task([] {
            RestampInventoryRowsFromLiveState();
        });
    }

    [[nodiscard]] std::optional<RingSelectionData> BuildSelectionFromEntry(RE::InventoryEntryData& a_entry) {
        auto* ring = Inventory::AsRing(a_entry.GetObject());
        if (!ring) {
            return std::nullopt;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto count = player ? Inventory::GetCount(*player, *ring) : 0;
        if (!player || count <= 0) {
            return std::nullopt;
        }

        auto customSelection = Inventory::ResolveEntryCustomSelection(a_entry);
        if (customSelection.failure != Inventory::EntryCustomFailure::kNone) {
            return RingSelectionData {
                .ring = ring,
                .blocked = true,
            };
        }

        if (customSelection.extraList
            && customSelection.key
            && customSelection.failure
            == Inventory::EntryCustomFailure::kNone) {
            const auto customIdentity = EnsureCustomSelectionIdentity(*ring, customSelection);
            if (!customIdentity) {
                return RingSelectionData {
                    .ring = ring,
                    .blocked = true,
                };
            }

            return RingSelectionData {
                .ring = ring,
                .customKey = customSelection.key,
                .customIdentity = customIdentity,
                .sourceExtraList = customSelection.extraList,
            };
        }

        return RingSelectionData {
            .ring = ring,
        };
    }

    enum class RingToggleResult {
        kFailed,
        kChanged,
        kHandled,
    };

    [[nodiscard]] bool IsSelectedRing(
        const RingSelectionData& a_selection,
        const RingTarget a_target,
        const RE::FormID a_formID
    ) {
        const auto selection = Selection::Get(a_target);
        bool selected = false;
        if (a_selection.customKey) {
            selected = selection.MatchesCustomEnchantment(a_formID, *a_selection.customKey, a_selection.customIdentity);
        } else {
            selected = selection.MatchesForm(a_formID);
        }

        return selected;
    }

    [[nodiscard]] std::uint32_t CountSelectedVirtualCopies(
        const RingSelectionData& a_selection,
        const std::optional<RingTarget> a_excludedTarget
    ) {
        auto count = std::uint32_t {0};
        const auto formID = a_selection.ring ? a_selection.ring->GetFormID() : RE::FormID {0};
        if (formID == 0) {
            return count;
        }

        for (const auto target : kVirtualRingTargets) {
            if (a_excludedTarget && *a_excludedTarget == target) {
                continue;
            }

            if (IsSelectedRing(a_selection, target, formID)) {
                ++count;
            }
        }

        return count;
    }

    [[nodiscard]] RingToggleResult MoveVanillaRingSlotFormToVirtual(
        const RingTarget a_target,
        RE::TESObjectARMO& a_ring,
        const SelectionOrigin a_origin
    ) {
        if (a_origin == SelectionOrigin::kInventoryMenu) {
            if (auto* inventoryMenu = GetInventoryMenu()) {
                DeselectInventoryItem(*inventoryMenu);
            }

            const auto result = Selection::MoveVanillaRingSlotFormToVirtual(a_ring.GetFormID(), a_target);
            if (result.inventoryChanged) {
                RefreshInventoryMenuAfterVanillaRingSlotMove();
            } else if (result.selectionChanged) {
                RefreshRingRows();
            }

            return result.ChangedState() ? RingToggleResult::kHandled : RingToggleResult::kFailed;
        }

        Selection::QueueVanillaRingSlotFormToVirtual(a_ring.GetFormID(), a_target);
        return RingToggleResult::kHandled;
    }

    [[nodiscard]] RingToggleResult MoveVanillaRingSlotCustomToVirtual(
        const RingTarget a_target,
        RE::TESObjectARMO& a_ring,
        const Inventory::CustomEnchantmentKey& a_customKey,
        const std::optional<Inventory::ExtraListIdentity>& a_customIdentity,
        const SelectionOrigin a_origin
    ) {
        if (a_origin == SelectionOrigin::kInventoryMenu) {
            if (auto* inventoryMenu = GetInventoryMenu()) {
                DeselectInventoryItem(*inventoryMenu);
            }

            const auto result = Selection::MoveVanillaRingSlotCustomToVirtual(
                a_ring.GetFormID(),
                a_customKey,
                a_customIdentity,
                a_target
            );
            if (result.inventoryChanged) {
                RefreshInventoryMenuAfterVanillaRingSlotMove();
            } else if (result.selectionChanged) {
                RefreshRingRows();
            }

            return result.ChangedState() ? RingToggleResult::kHandled : RingToggleResult::kFailed;
        }

        Selection::QueueVanillaRingSlotCustomToVirtual(a_ring.GetFormID(), a_customKey, a_customIdentity, a_target);
        return RingToggleResult::kHandled;
    }

    [[nodiscard]] RingToggleResult SelectCustomRing(
        RE::PlayerCharacter& a_player,
        const RingTarget a_target,
        RE::TESObjectARMO& a_ring,
        const RingSelectionData& a_selection,
        const Inventory::CustomEnchantmentKey& a_customKey,
        const SelectionOrigin a_origin
    ) {
        const auto
            sourceMatches = Inventory::FindSourceMatches(a_player, a_ring, a_customKey, a_selection.customIdentity);
        auto* sourceExtraList = sourceMatches.firstExtraList;
        if (a_selection.sourceExtraList
            && Inventory::MatchesCustomSelection(
                a_selection.sourceExtraList,
                a_customKey,
                a_selection.customIdentity
            )) {
            sourceExtraList = a_selection.sourceExtraList;
        }
        if (!sourceExtraList || !sourceMatches.HasMatch()) {
            return RingToggleResult::kFailed;
        }

        if (!Inventory::MatchesCustomSelection(sourceExtraList, a_customKey, a_selection.customIdentity)) {
            return RingToggleResult::kFailed;
        }

        const auto selectedCopies = CountSelectedVirtualCopies(a_selection, a_target);
        if (sourceMatches.rightWornExtraList && std::cmp_less_equal(sourceMatches.count, selectedCopies + 1)) {
            return MoveVanillaRingSlotCustomToVirtual(
                a_target,
                a_ring,
                a_customKey,
                a_selection.customIdentity,
                a_origin
            );
        }

        if (!sourceMatches.rightWornExtraList && std::cmp_less_equal(sourceMatches.count, selectedCopies)) {
            return RingToggleResult::kFailed;
        }

        const auto changed = Selection::SetCustom(a_ring, a_customKey, a_selection.customIdentity, a_target);
        return changed ? RingToggleResult::kChanged : RingToggleResult::kFailed;
    }

    [[nodiscard]] RingToggleResult SelectFormRing(
        RE::PlayerCharacter& a_player,
        const RingTarget a_target,
        RE::TESObjectARMO& a_ring,
        const RingSelectionData& a_selection,
        const SelectionOrigin a_origin
    ) {
        const auto sourceMatches = Inventory::FindFormOnlyMatches(a_player, a_ring);
        if (!sourceMatches.HasMatch()) {
            RefreshItemRowsForRing(a_player, std::addressof(a_ring));
            return RingToggleResult::kHandled;
        }

        const auto selectedCopies = CountSelectedVirtualCopies(a_selection, a_target);
        if (sourceMatches.rightWorn && std::cmp_less_equal(sourceMatches.count, selectedCopies + 1)) {
            return MoveVanillaRingSlotFormToVirtual(a_target, a_ring, a_origin);
        }

        if (!sourceMatches.rightWorn && std::cmp_less_equal(sourceMatches.count, selectedCopies)) {
            return RingToggleResult::kFailed;
        }

        const auto changed = Selection::Set(std::addressof(a_ring), a_target);
        return changed ? RingToggleResult::kChanged : RingToggleResult::kFailed;
    }

    [[nodiscard]] RingToggleResult ToggleVanillaRingSlot(
        const RingSelectionData& a_selection,
        const SelectionOrigin a_origin
    ) {
        auto* ring = a_selection.ring;
        if (!ring) {
            return RingToggleResult::kFailed;
        }

        const auto result = a_selection.customKey ? Selection::ToggleVanillaRingSlotCustom(
                                                        ring->GetFormID(),
                                                        *a_selection.customKey,
                                                        a_selection.customIdentity
                                                    )
                                                  : Selection::ToggleVanillaRingSlotForm(ring->GetFormID());
        if (!result.ChangedState()) {
            return RingToggleResult::kFailed;
        }

        if (a_origin == SelectionOrigin::kInventoryMenu && result.inventoryChanged) {
            RefreshInventoryMenuAfterVanillaRingSlotMove();
            return RingToggleResult::kHandled;
        }

        if (result.inventoryChanged) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                RefreshItemRowsForRing(*player, ring);
            }
            return RingToggleResult::kHandled;
        }

        RefreshRingRows();
        return RingToggleResult::kHandled;
    }

    bool ToggleRingForTarget(
        const RingSelectionData& a_selection,
        const RingTarget a_target,
        const SelectionOrigin a_origin
    ) {
        auto* ring = a_selection.ring;
        if (!ring || a_selection.blocked) {
            return false;
        }

        if (a_target == kVanillaRingTarget) {
            const auto result = ToggleVanillaRingSlot(a_selection, a_origin);
            return result != RingToggleResult::kFailed;
        }

        const auto formID = ring->GetFormID();
        auto sound = RingSounds::Event::kNone;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (IsSelectedRing(a_selection, a_target, formID)) {
            Selection::Clear(a_target);
            sound = RingSounds::Event::kUnequip;
        } else {
            if (!player) {
                return false;
            }

            auto result = RingToggleResult::kFailed;
            if (a_selection.customKey) {
                result = SelectCustomRing(*player, a_target, *ring, a_selection, *a_selection.customKey, a_origin);
            } else {
                result = SelectFormRing(*player, a_target, *ring, a_selection, a_origin);
            }
            if (result == RingToggleResult::kFailed) {
                return false;
            }

            if (result == RingToggleResult::kHandled) {
                return true;
            }
            sound = RingSounds::Event::kEquip;
        }

        VirtualRings::RequestRefresh(
            VirtualRings::RefreshOptions {
                .soundTarget = a_target,
                .sound = sound,
            }
        );
        RefreshRingRows();
        return true;
    }

    [[nodiscard]] RingTarget DefaultTargetForHand(const RingHand a_hand) {
        return a_hand == RingHand::kLeft ? kDefaultLeftEquipTarget : kVanillaRingTarget;
    }

    [[nodiscard]] StoredRingSelection StoreSelection(
        const RingSelectionData& a_selection,
        const SelectionOrigin a_origin
    ) {
        return StoredRingSelection {
            .sourceFormID = a_selection.ring ? a_selection.ring->GetFormID() : RE::FormID {0},
            .customKey = a_selection.customKey,
            .customIdentity = a_selection.customIdentity,
            .origin = a_origin,
        };
    }

    [[nodiscard]] std::optional<RingSelectionData> RestoreSelection(const StoredRingSelection& a_selection) {
        auto* ring = Inventory::AsRing(RE::TESForm::LookupByID<RE::TESObjectARMO>(a_selection.sourceFormID));
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!ring || !player || Inventory::GetCount(*player, *ring) <= 0) {
            return std::nullopt;
        }

        if (a_selection.customKey) {
            const auto sourceMatches = Inventory::FindSourceMatches(
                *player,
                *ring,
                *a_selection.customKey,
                a_selection.customIdentity
            );
            if (!sourceMatches.HasMatch()) {
                return std::nullopt;
            }
        }

        return RingSelectionData {
            .ring = ring,
            .customKey = a_selection.customKey,
            .customIdentity = a_selection.customIdentity,
        };
    }

    [[nodiscard]] bool IsRingInVanillaRingSlot(const RingSelectionData& a_selection) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !a_selection.ring) {
            return false;
        }

        if (a_selection.customKey) {
            return Inventory::FindSourceMatches(
                       *player,
                       *a_selection.ring,
                       *a_selection.customKey,
                       a_selection.customIdentity
                   )
                       .rightWornExtraList
                   != nullptr;
        }

        return Inventory::FindFormOnlyMatches(*player, *a_selection.ring).rightWorn;
    }

    [[nodiscard]] bool IsSelectedRingTarget(const RingSelectionData& a_selection, const RingTarget a_target) {
        if (!a_selection.ring) {
            return false;
        }

        if (a_target == kVanillaRingTarget) {
            return IsRingInVanillaRingSlot(a_selection);
        }

        return IsSelectedRing(a_selection, a_target, a_selection.ring->GetFormID());
    }

    [[nodiscard]] std::vector<RingTarget> CollectSelectedTargetsOnHand(
        const RingSelectionData& a_selection,
        const RingHand a_hand
    ) {
        std::vector<RingTarget> targets;
        targets.reserve(FingerSelectMenu::kRowCount);

        for (const auto target : kVirtualRingTargets) {
            if (target.hand == a_hand && IsSelectedRingTarget(a_selection, target)) {
                targets.push_back(target);
            }
        }

        if (a_hand == RingHand::kRight && IsSelectedRingTarget(a_selection, kVanillaRingTarget)) {
            targets.push_back(kVanillaRingTarget);
        }

        return targets;
    }

    [[nodiscard]] bool ShouldOpenFingerSelectorForHand(const RingSelectionData& a_selection, const RingHand a_hand) {
        const auto selectedTargets = CollectSelectedTargetsOnHand(a_selection, a_hand);
        if (selectedTargets.empty()) {
            return false;
        }

        return selectedTargets.size() > 1 || selectedTargets.front() != DefaultTargetForHand(a_hand);
    }

    [[nodiscard]] std::optional<RingTarget> FindPreferredSelectedTargetOnHand(
        const RingSelectionData& a_selection,
        const RingHand a_hand
    ) {
        const auto selectedTargets = CollectSelectedTargetsOnHand(a_selection, a_hand);
        const auto nonIndexTarget = std::ranges::find_if(selectedTargets, [](const auto target) {
            return target.finger != RingFinger::kIndex;
        });
        if (nonIndexTarget != selectedTargets.end()) {
            return *nonIndexTarget;
        }

        if (!selectedTargets.empty()) {
            return selectedTargets.front();
        }

        return std::nullopt;
    }

    [[nodiscard]] std::string GetRingName(const RE::TESObjectARMO& a_ring) {
        const auto* name = a_ring.GetName();
        return name && name[0] != '\0' ? name : kEmptyRingLabel;
    }

    [[nodiscard]] std::string GetSourceRingLabel(const RingSelectionData& a_selection) {
        if (a_selection.customKey && !a_selection.customKey->playerDisplayName.empty()) {
            return a_selection.customKey->playerDisplayName;
        }

        return a_selection.ring ? GetRingName(*a_selection.ring) : kEmptyRingLabel;
    }

    [[nodiscard]] std::string GetVirtualTargetRingLabel(const RingTarget a_target) {
        const auto selection = Selection::Get(a_target);
        if (selection.kind == Selection::Kind::kNone || selection.sourceFormID == 0) {
            return kEmptyRingLabel;
        }

        if (selection.kind == Selection::Kind::kCustomEnchantment && !selection.customKey.playerDisplayName.empty()) {
            return selection.customKey.playerDisplayName;
        }

        const auto* ring = Inventory::AsRing(RE::TESForm::LookupByID(selection.sourceFormID));
        return ring ? GetRingName(*ring) : kEmptyRingLabel;
    }

    [[nodiscard]] std::optional<std::string> GetRightWornExtraListLabel(
        const RE::InventoryEntryData& a_entry,
        const RE::TESObjectARMO& a_ring
    ) {
        if (!a_entry.extraLists) {
            return std::nullopt;
        }

        for (auto* extraList : *a_entry.extraLists) {
            if (!Inventory::IsRightWorn(extraList)) {
                continue;
            }

            if (extraList) {
                const auto custom = Inventory::ReadCustomEnchantment(*extraList);
                if (custom && custom->playerDisplayName && !custom->playerDisplayName->empty()) {
                    return *custom->playerDisplayName;
                }
            }

            return GetRingName(a_ring);
        }

        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> GetRightWornEntryLabel(RE::InventoryEntryData& a_entry) {
        auto* ring = Inventory::AsRing(a_entry.GetObject());
        if (!ring) {
            return std::nullopt;
        }

        if (auto label = GetRightWornExtraListLabel(a_entry, *ring)) {
            return label;
        }

        if (!a_entry.extraLists && a_entry.IsWorn(false)) {
            return GetRingName(*ring);
        }

        return std::nullopt;
    }

    [[nodiscard]] std::string GetRightWornRingLabel() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* inventoryChanges = player ? player->GetInventoryChanges() : nullptr;
        if (!inventoryChanges || !inventoryChanges->entryList) {
            return kEmptyRingLabel;
        }

        for (auto* entry : *inventoryChanges->entryList) {
            if (entry) {
                if (auto label = GetRightWornEntryLabel(*entry)) {
                    return *label;
                }
            }
        }

        return kEmptyRingLabel;
    }

    [[nodiscard]] std::string GetEquippedRingLabel(const RingTarget a_target) {
        if (a_target == kVanillaRingTarget) {
            return GetRightWornRingLabel();
        }

        return GetVirtualTargetRingLabel(a_target);
    }

    [[nodiscard]] bool IsFingerTargetOccupied(const RingTarget a_target) {
        if (a_target == kVanillaRingTarget) {
            return GetRightWornRingLabel() != kEmptyRingLabel;
        }

        return Selection::Get(a_target).kind != Selection::Kind::kNone;
    }

    [[nodiscard]] std::string GetFingerRowActionLabel(const RingSelectionData& a_selection, const RingTarget a_target) {
        if (IsSelectedRingTarget(a_selection, a_target)) {
            return kUnequipActionLabel;
        }

        if (IsFingerTargetOccupied(a_target)) {
            return kReplaceActionLabel;
        }

        return kEquipActionLabel;
    }

    [[nodiscard]] std::array<FingerSelectMenu::Row, FingerSelectMenu::kRowCount> BuildFingerRows(
        const RingSelectionData& a_selection,
        const RingHand a_hand
    ) {
        constexpr std::array kFingerOrder {
            RingFinger::kThumb,
            RingFinger::kIndex,
            RingFinger::kMiddle,
            RingFinger::kRing,
            RingFinger::kPinky,
        };

        std::array<FingerSelectMenu::Row, FingerSelectMenu::kRowCount> rows;
        for (std::size_t index = 0; index < rows.size(); ++index) {
            const auto finger = kFingerOrder[index];
            const auto target = RingTarget {
                .hand = a_hand,
                .finger = finger,
            };
            rows[index] = FingerSelectMenu::Row {
                .target = target,
                .fingerLabel = std::string {FingerLabel(finger)},
                .equippedRingLabel = GetEquippedRingLabel(target),
                .actionLabel = GetFingerRowActionLabel(a_selection, target),
            };
        }
        return rows;
    }

    [[nodiscard]] std::size_t GetRowIndex(
        const std::array<FingerSelectMenu::Row, FingerSelectMenu::kRowCount>& a_rows,
        const RingTarget a_target
    ) {
        const auto it = std::ranges::find_if(a_rows, [a_target](const auto& a_row) {
            return a_row.target == a_target;
        });
        return it == a_rows.end() ? 0 : static_cast<std::size_t>(std::distance(a_rows.begin(), it));
    }

    void ApplyStoredSelection(const StoredRingSelection& a_selection, const RingTarget a_target) {
        auto selection = RestoreSelection(a_selection);
        if (!selection) {
            return;
        }

        ToggleRingForTarget(*selection, a_target, a_selection.origin);
    }

    bool ShowFingerSelector(
        const RingSelectionData& a_selection,
        const RingHand a_hand,
        const SelectionOrigin a_origin,
        const RE::INPUT_DEVICE a_inputDevice
    ) {
        auto* ring = a_selection.ring;
        if (!ring) {
            return false;
        }

        if (RingFootprints::GetSourceRingFootprint(*ring).IsMultiFinger()) {
            RE::SendHUDMessage::ShowHUDMessage("This ring occupies multiple fingers.", nullptr, true);
            const auto target = DefaultTargetForHand(a_hand);
            return ToggleRingForTarget(a_selection, target, a_origin);
        }

        auto rows = BuildFingerRows(a_selection, a_hand);
        const auto selectedTarget = FindPreferredSelectedTargetOnHand(a_selection, a_hand)
                                        .value_or(DefaultTargetForHand(a_hand));
        const auto startIndex = GetRowIndex(rows, selectedTarget);
        const auto storedSelection = StoreSelection(a_selection, a_origin);

        return FingerSelectMenu::Show(
            FingerSelectMenu::Data {
                .title = kFingerSelectTitle,
                .ringName = GetSourceRingLabel(a_selection),
                .rows = std::move(rows),
                .selectedIndex = startIndex,
                .inputDevice = a_inputDevice,
                .hostMenu = a_origin == SelectionOrigin::kFavoritesMenu ? FingerSelectMenu::Data::HostMenu::kFavorites
                                                                        : FingerSelectMenu::Data::HostMenu::kInventory,
                .onResult = [storedSelection](const FingerSelectMenu::Result a_result) {
                    if (a_result.action == FingerSelectMenu::Result::Action::kCancel || !a_result.target) {
                        return;
                    }

                    stl::add_task([storedSelection, target = *a_result.target] {
                        ApplyStoredSelection(storedSelection, target);
                    });
                },
            }
        );
    }

    [[nodiscard]] RE::BSWin32KeyboardDevice* GetKeyboard(RE::BSInputDeviceManager& a_input) {
        auto* device = a_input.devices[std::to_underlying(RE::INPUT_DEVICE::kKeyboard)];
        return device ? stl::unrestricted_cast<RE::BSWin32KeyboardDevice*>(device) : nullptr;
    }

    [[nodiscard]] bool IsKeyboardKeyPressed(
        const RE::BSWin32KeyboardDevice& a_keyboard,
        const RE::BSKeyboardDevice::Key a_key
    ) {
        const auto& keys = a_keyboard.GetRuntimeData().curState;
        const auto key = static_cast<std::uint32_t>(a_key);
        return key < std::size(keys) && (keys[key] & 0x80) != 0;
    }

    [[nodiscard]] bool IsShiftDown() {
        auto* input = RE::BSInputDeviceManager::GetSingleton();
        auto* keyboard = input ? GetKeyboard(*input) : nullptr;
        return keyboard
               && (IsKeyboardKeyPressed(*keyboard, RE::BSKeyboardDevice::Key::kLeftShift)
                   || IsKeyboardKeyPressed(*keyboard, RE::BSKeyboardDevice::Key::kRightShift));
    }

    [[nodiscard]] RE::BSPCGamepadDeviceDelegate* GetGamepad(RE::BSInputDeviceManager& a_input) {
        auto* device = a_input.devices[std::to_underlying(RE::INPUT_DEVICE::kGamepad)];
        auto* handler = device ? stl::unrestricted_cast<RE::BSPCGamepadDeviceHandler*>(device) : nullptr;
        return handler ? handler->GetRuntimeData().currentPCGamePadDelegate : nullptr;
    }

    [[nodiscard]] bool IsGamepadKeyPressed(RE::BSPCGamepadDeviceDelegate& a_gamepad, const std::uint32_t a_key) {
        const auto& buttons = static_cast<RE::BSInputDevice&>(a_gamepad).GetRuntimeData().deviceButtons;
        const auto it = buttons.find(a_key);
        return it != buttons.end() && it->second && it->second->heldDownSecs > 0.0F;
    }

    [[nodiscard]] bool IsGamepadSprintDown() {
        auto* controlMap = RE::ControlMap::GetSingleton();
        auto* input = RE::BSInputDeviceManager::GetSingleton();
        if (!controlMap || !input) {
            return false;
        }

        const auto key = controlMap->GetMappedKey(
            "Sprint",
            RE::INPUT_DEVICE::kGamepad,
            RE::UserEvents::INPUT_CONTEXT_ID::kGameplay
        );
        auto* gamepad = GetGamepad(*input);
        return key != RE::ControlMap::kInvalid && gamepad && IsGamepadKeyPressed(*gamepad, key);
    }

    [[nodiscard]] RE::INPUT_DEVICE GetPreferredInputDevice() {
        auto* input = RE::BSInputDeviceManager::GetSingleton();
        return input && GetGamepad(*input) ? RE::INPUT_DEVICE::kGamepad : RE::INPUT_DEVICE::kKeyboard;
    }

    [[nodiscard]] FingerSelectTrigger GetFingerSelectTrigger() {
        if (Settings::GetSingleton()->AlwaysChooseFinger()) {
            const auto inputDevice = GetPreferredInputDevice();
            return FingerSelectTrigger {
                .requested = true,
                .inputDevice = inputDevice,
            };
        }

        if (IsShiftDown()) {
            return FingerSelectTrigger {
                .requested = true,
                .inputDevice = RE::INPUT_DEVICE::kKeyboard,
            };
        }

        if (IsGamepadSprintDown()) {
            return FingerSelectTrigger {
                .requested = true,
                .inputDevice = RE::INPUT_DEVICE::kGamepad,
            };
        }

        return {};
    }

    class MenuEventSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        static MenuEventSink* GetSingleton() {
            static MenuEventSink sink;
            return std::addressof(sink);
        }

        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent* a_event,
            [[maybe_unused]] RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource
        ) override {
            if (!a_event) {
                return RE::BSEventNotifyControl::kContinue;
            }

            if (a_event->menuName == RE::InventoryMenu::MENU_NAME.data()) {
                if (a_event->opening) {
                    QueueInventoryRefresh();
                }
                return RE::BSEventNotifyControl::kContinue;
            }

            if (a_event->menuName == RE::FavoritesMenu::MENU_NAME.data()) {
                if (a_event->opening) {
                    RefreshFavoritesRows();
                }
            }

            return RE::BSEventNotifyControl::kContinue;
        }
    };

}

void InstallMenuEventSink() {
    if (auto* ui = RE::UI::GetSingleton()) {
        ui->AddEventSink(MenuEventSink::GetSingleton());
        logger::info("UI: menu event sink installed");
    } else {
        logger::warn("UI: menu event sink skipped | reason=noUI");
    }
}

void RegisterInventoryData() {
    if (const auto* scaleform = SKSE::GetScaleformInterface()) {
        scaleform->Register(InventoryDataCallback);
        logger::info("UI: inventory data callback registered");
    } else {
        logger::warn("UI: inventory data callback skipped | reason=noScaleform");
    }
}

bool UseRingFromMenuEntry(RE::InventoryEntryData* a_entry, const RingHand a_hand, const SelectionOrigin a_origin) {
    if (FingerSelectMenu::IsPendingOrOpen()) {
        return true;
    }

    if (!a_entry) {
        return false;
    }

    auto selection = BuildSelectionFromEntry(*a_entry);
    if (!selection) {
        return false;
    }

    if (selection->blocked) {
        return true;
    }

    const auto trigger = GetFingerSelectTrigger();
    if (trigger.requested) {
        ShowFingerSelector(*selection, a_hand, a_origin, trigger.inputDevice);
        return true;
    }

    if (ShouldOpenFingerSelectorForHand(*selection, a_hand)) {
        ShowFingerSelector(*selection, a_hand, a_origin, GetPreferredInputDevice());
        return true;
    }

    if (a_hand == RingHand::kRight) {
        return false;
    }

    return ToggleRingForTarget(*selection, kDefaultLeftEquipTarget, a_origin);
}

void RefreshRingRows() {
    QueueInventoryRefresh();
    RefreshFavoritesRows();
}

void RefreshFavoritesRows() {
    stl::add_ui_task([] {
        RestampFavoritesRowsFromEntryData();
    });
}

void RefreshInventoryMenuAfterVanillaRingSlotMove() {
    auto* inventoryMenu = GetInventoryMenu();
    if (!inventoryMenu) {
        return;
    }

    DeselectInventoryItem(*inventoryMenu);
    if (auto* itemList = inventoryMenu->GetRuntimeData().itemList) {
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            itemList->Update(player);
        }
    }
    DeselectInventoryItem(*inventoryMenu);
    InvalidateInventoryListData(*inventoryMenu);
    RefreshFavoritesRows();
}

void RefreshItemRowsForRing(RE::Actor& a_actor, const RE::TESObjectARMO* a_ring) {
    RE::SendUIMessage::SendInventoryUpdateMessage(std::addressof(a_actor), a_ring);
    RefreshFavoritesRows();
}

void QueueRefreshAfterRingEquip() {
    stl::add_task([] {
        VirtualRings::RequestRefresh();
        RefreshFavoritesRows();
    });
}
}
