#include "UI/InventoryMenu.h"

#include "Localization.h"
#include "Settings.h"
#include "UI/FavoritesMenu.h"
#include "UI/RingItemRows.h"
#include "UI/Scaleform.h"

#include <RE/G/GFxFunctionHandler.h>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>

namespace UI::InventoryMenu {
namespace {
    constexpr auto kScaleformInventoryEquipHintPatched = "lhrsInventoryEquipHintPatched";
    constexpr auto kScaleformInventoryFingerSelectHintPatched = "lhrsInventoryFingerSelectHintPatched";
    constexpr auto kInventoryMenuClipPath = "_root.Menu_mc";
    constexpr auto kSkyUISelectedEntryPath = "_root.Menu_mc.inventoryLists.itemList.selectedEntry";
    constexpr auto kSkyUIUpdateBottomBarPath = "_root.Menu_mc.updateBottomBar";
    constexpr auto kSkyUINavPanelAddButtonPath = "_root.Menu_mc.navPanel.addButton";
    constexpr auto kVanillaSelectedEntryPath = "_root.Menu_mc.InventoryLists_mc.ItemsList.selectedEntry";
    constexpr auto kVanillaUpdateBottomBarPath = "_root.Menu_mc.UpdateBottomBarButtons";
    constexpr auto kSkyUIInvalidateListDataPath = "_root.Menu_mc.inventoryLists.InvalidateListData";
    constexpr auto kSkyUIItemMenuContext = 3;
    constexpr auto kInventoryFingerHintKey = "$LHRS_Inventory_FingerHint";

    [[nodiscard]] RE::InventoryMenu* GetOpenInventoryMenu() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui || !ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME)) {
            return nullptr;
        }

        auto inventoryMenu = ui->GetMenu<RE::InventoryMenu>();
        return inventoryMenu.get();
    }

    [[nodiscard]] std::optional<RE::GFxValue> GetSelectedEntry(const RE::GFxMovie& a_movie, const char* a_path) {
        RE::GFxValue selectedEntry;
        if (!a_movie.GetVariable(std::addressof(selectedEntry), a_path) || !Scaleform::CanReadMembers(selectedEntry)) {
            return std::nullopt;
        }

        return selectedEntry;
    }

    [[nodiscard]] bool IsSelectedRingEquipHintRow(const RE::GFxMovie& a_movie, const char* a_path) {
        const auto selectedEntry = GetSelectedEntry(a_movie, a_path);
        return selectedEntry && RingItemRows::CanUseRingEquipHint(*selectedEntry);
    }

    [[nodiscard]] bool IsSelectedFingerSelectHintRow(const RE::GFxMovie& a_movie) {
        const auto selectedEntry = GetSelectedEntry(a_movie, kSkyUISelectedEntryPath);
        return selectedEntry && RingItemRows::CanShowFingerSelectHint(*selectedEntry);
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

        const auto platform = Scaleform::ReadIntMember(a_inventoryMenu, "_platform").value_or(0);
        const auto keyCode = platform == 0 ? Settings::GetSingleton()->GetFingerSelectModifierKey()
                                           : Settings::GetSingleton()->GetFingerSelectModifierButton();
        a_control.SetMember("keyCode", static_cast<int>(keyCode));
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
        const auto label = Localization::Translate(kInventoryFingerHintKey, "Finger");
        text.SetString(label.c_str());
        a_result.SetMember("text", text);

        RE::GFxValue controls;
        SetSkyUIFingerSelectHintControl(a_movie, controls, a_inventoryMenu);
        a_result.SetMember("controls", controls);
    }

    void SetVanillaRingEquipButtonArt(RE::GFxValue& a_inventoryMenu) {
        RE::GFxValue bottomBar;
        if (!a_inventoryMenu.GetMember("BottomBar_mc", std::addressof(bottomBar))
            || !Scaleform::CanReadMembers(bottomBar)) {
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
            if (IsSelectedRingEquipHintRow(*a_params.movie, kSkyUISelectedEntryPath)) {
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
                && IsSelectedFingerSelectHintRow(*a_params.movie)
                && a_params.thisPtr->GetMember("navPanel", std::addressof(navPanel))
                && Scaleform::CanReadMembers(navPanel)
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

            if (IsSelectedRingEquipHintRow(*a_params.movie, kVanillaSelectedEntryPath)) {
                SetVanillaRingEquipButtonArt(*a_params.thisPtr);
            }
        }

    private:
        RE::GFxValue originalFunction_;
    };

    template <class Handler>
    [[nodiscard]] bool InstallScaleformFunctionPatch(
        RE::GFxMovie& a_movie,
        RE::GFxValue& a_menu,
        const char* a_installedFlag,
        const char* a_memberName,
        const std::initializer_list<const char*> a_requiredPaths
    ) {
        if (Scaleform::ReadBoolMember(a_menu, a_installedFlag).value_or(false)) {
            return false;
        }

        for (const auto* path : a_requiredPaths) {
            if (!a_movie.IsAvailable(path)) {
                return false;
            }
        }

        RE::GFxValue originalFunction;
        if (!a_menu.GetMember(a_memberName, std::addressof(originalFunction)) || !originalFunction.IsObject()) {
            return false;
        }

        auto handler = RE::make_gptr<Handler>(originalFunction);
        RE::GFxValue function;
        a_movie.CreateFunction(std::addressof(function), handler.get());
        if (!a_menu.SetMember(a_memberName, function)) {
            return false;
        }

        a_menu.SetMember(a_installedFlag, true);
        return true;
    }

    [[nodiscard]] bool InstallInventoryMenuButtonHints(RE::InventoryMenu& a_inventoryMenu) {
        auto* movie = a_inventoryMenu.uiMovie.get();
        if (!movie) {
            return false;
        }

        RE::GFxValue inventoryMenu;
        if (!movie->GetVariable(std::addressof(inventoryMenu), kInventoryMenuClipPath)
            || !Scaleform::CanReadMembers(inventoryMenu)) {
            return false;
        }

        auto installed = InstallScaleformFunctionPatch<SkyUIEquipButtonDataHandler>(
            *movie,
            inventoryMenu,
            kScaleformInventoryEquipHintPatched,
            "getEquipButtonData",
            {kSkyUIUpdateBottomBarPath}
        );
        installed = InstallScaleformFunctionPatch<SkyUIBottomBarUpdateHandler>(
                        *movie,
                        inventoryMenu,
                        kScaleformInventoryFingerSelectHintPatched,
                        "updateBottomBar",
                        {kSkyUIUpdateBottomBarPath, kSkyUINavPanelAddButtonPath}
                    )
                    || installed;
        installed = InstallScaleformFunctionPatch<VanillaBottomBarUpdateHandler>(
                        *movie,
                        inventoryMenu,
                        kScaleformInventoryEquipHintPatched,
                        "UpdateBottomBarButtons",
                        {kVanillaUpdateBottomBarPath}
                    )
                    || installed;
        return installed;
    }

    void RefreshInventoryMenuButtonHints(RE::InventoryMenu& a_inventoryMenu) {
        auto* movie = a_inventoryMenu.uiMovie.get();
        if (!movie) {
            return;
        }

        if (movie->IsAvailable(kSkyUIUpdateBottomBarPath)) {
            std::array<RE::GFxValue, 1> args;
            args[0].SetBoolean(GetSelectedEntry(*movie, kSkyUISelectedEntryPath).has_value());
            static_cast<void>(
                movie->Invoke(kSkyUIUpdateBottomBarPath, nullptr, args.data(), static_cast<std::uint32_t>(args.size()))
            );
            return;
        }

        if (movie->IsAvailable(kVanillaUpdateBottomBarPath)
            && GetSelectedEntry(*movie, kVanillaSelectedEntryPath).has_value()) {
            static_cast<void>(movie->Invoke(kVanillaUpdateBottomBarPath, nullptr, nullptr, 0));
        }
    }

    void InvalidateSkyUIInventoryListData(RE::InventoryMenu& a_menu) {
        auto* movie = a_menu.uiMovie.get();
        if (!movie) {
            return;
        }

        if (!movie->IsAvailable(kSkyUIInvalidateListDataPath)) {
            return;
        }

        static_cast<void>(movie->Invoke(kSkyUIInvalidateListDataPath, nullptr, nullptr, 0));
    }

    void DeselectItem(RE::InventoryMenu& a_menu) {
        auto* itemList = a_menu.GetRuntimeData().itemList;
        if (!itemList) {
            return;
        }
        if (!Scaleform::CanReadMembers(itemList->root)) {
            return;
        }

        static_cast<void>(itemList->root.SetMember("selectedIndex", RE::GFxValue {-1.0}));
    }

    void RefreshOpenInventoryRingRows() {
        auto* inventoryMenu = GetOpenInventoryMenu();
        if (!inventoryMenu) {
            return;
        }

        const auto buttonHintsInstalled = InstallInventoryMenuButtonHints(*inventoryMenu);

        auto* itemList = inventoryMenu->GetRuntimeData().itemList;
        if (!itemList || !itemList->entryList.IsArray()) {
            return;
        }

        std::uint32_t changedEntryRows = 0;
        for (std::uint32_t index = 0; index < itemList->entryList.GetArraySize(); ++index) {
            RE::GFxValue entryObject;
            if (!itemList->entryList.GetElement(index, std::addressof(entryObject))
                || !Scaleform::CanReadMembers(entryObject)) {
                continue;
            }

            const auto result = RingItemRows::RefreshStampedRingEntry(entryObject, Core::GetPlayerActorKey());
            if (result != RingItemRows::RowStampResult::kChanged) {
                continue;
            }

            ++changedEntryRows;
            static_cast<void>(itemList->entryList.SetElement(index, entryObject));
        }

        if (changedEntryRows > 0) {
            InvalidateSkyUIInventoryListData(*inventoryMenu);
        }

        if (buttonHintsInstalled) {
            RefreshInventoryMenuButtonHints(*inventoryMenu);
        }
    }

    void RefreshOpenInventoryRowsFromRuntime() {
        auto* inventoryMenu = GetOpenInventoryMenu();
        if (!inventoryMenu) {
            return;
        }

        auto* itemList = inventoryMenu->GetRuntimeData().itemList;
        if (!itemList) {
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }

        itemList->Update(player);
    }

    void RefreshAfterVanillaRingSlotMoveOnUIThread() {
        auto* inventoryMenu = GetOpenInventoryMenu();
        if (!inventoryMenu) {
            return;
        }

        DeselectItem(*inventoryMenu);
        if (auto* itemList = inventoryMenu->GetRuntimeData().itemList) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                itemList->Update(player);
            }
        }
        DeselectItem(*inventoryMenu);
        InvalidateSkyUIInventoryListData(*inventoryMenu);
        FavoritesMenu::QueueRingRowRefresh();
    }
}

void QueueOpenMenuRingRowRefresh() {
    stl::add_ui_task([] {
        RefreshOpenInventoryRingRows();
    });
}

void QueueRingRowRefresh() {
    stl::add_ui_task([] {
        RefreshOpenInventoryRowsFromRuntime();
    });
}

void QueueRefreshAfterVanillaRingSlotMove() {
    stl::add_ui_task([] {
        RefreshAfterVanillaRingSlotMoveOnUIThread();
    });
}
}
