#include "UI/InventoryMenu.h"

#include "Localization.h"
#include "Settings.h"
#include "UI/RingItemRows.h"
#include "UI/Scaleform.h"
#include "UI/VanillaItemMenuControls.h"

#include <RE/G/GFxFunctionHandler.h>
#include <RE/I/InventoryMenu.h>
#include <RE/I/ItemList.h>
#include <RE/S/SendUIMessage.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <vector>

namespace UI::InventoryMenu {
namespace {
    enum class InventoryMenuFlavor : std::uint8_t {
        kUnknown,
        kSkyUI,
        kVanilla,
    };

    constexpr std::ptrdiff_t kRuntimeDataOffset = 0x30;
    constexpr std::ptrdiff_t kVRRuntimeDataOffset = 0x58;

    constexpr auto kScaleformInventoryEquipHintPatched = "lhrsInventoryEquipHintPatched";
    constexpr auto kScaleformInventoryFingerSelectHintPatched = "lhrsInventoryFingerSelectHintPatched";
    constexpr auto kInventoryMenuClipPath = "_root.Menu_mc";
    constexpr auto kSkyUISelectedEntryPath = "_root.Menu_mc.inventoryLists.itemList.selectedEntry";
    constexpr auto kSkyUIUpdateBottomBarPath = "_root.Menu_mc.updateBottomBar";
    constexpr auto kSkyUINavPanelAddButtonPath = "_root.Menu_mc.navPanel.addButton";
    constexpr auto kVanillaSelectedEntryPath = "_root.Menu_mc.InventoryLists_mc.ItemsList.selectedEntry";
    constexpr auto kVanillaUpdateBottomBarPath = "_root.Menu_mc.UpdateBottomBarButtons";
    constexpr auto kVanillaAltButtonArt = "AltButtonArt";
    constexpr auto kVanillaEquipButtonArt = "EquipButtonArt";
    constexpr auto kSkyUIInvalidateListDataPath = "_root.Menu_mc.inventoryLists.InvalidateListData";
    constexpr auto kVanillaInvalidateListDataPath = "_root.Menu_mc.InventoryLists_mc.InvalidateListData";
    constexpr auto kSkseExtendDataPath = "_global.skse.ExtendData";
    constexpr auto kSkyUIItemMenuContext = 3;
    constexpr auto kInventoryFingerHintKey = "$LHRS_Inventory_FingerHint";
    constexpr auto kVanillaInventoryShiftedButtonCount = 3U;
    constexpr auto kVanillaInventoryPreservedButtonArtFirstIndex = 1U;
    constexpr auto kVanillaInventoryPreservedButtonArtCount = 3U;
    constexpr auto kPendingRestampRingRows = 1U << 0;
    constexpr auto kPendingRefreshButtonHints = 1U << 1;

    std::atomic<InventoryMenuFlavor> lastOpenedMenuFlavor {InventoryMenuFlavor::kUnknown};
    std::atomic_uint32_t pendingInventoryUpdateRefresh {0};

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

    [[nodiscard]] bool IsSelectedFingerSelectHintRow(const RE::GFxMovie& a_movie, const char* a_path) {
        const auto selectedEntry = GetSelectedEntry(a_movie, a_path);
        return selectedEntry && RingItemRows::CanShowFingerSelectHint(*selectedEntry);
    }

    [[nodiscard]] std::optional<VanillaItemMenuControls::ButtonArt> GetVisibleVanillaFingerSelectHintArt(
        const RE::GFxMovie& a_movie,
        const RE::GFxValue& a_inventoryMenu
    ) {
        if (Settings::GetSingleton()->AlwaysChooseFinger()
            || !IsSelectedFingerSelectHintRow(a_movie, kVanillaSelectedEntryPath)) {
            return std::nullopt;
        }

        return VanillaItemMenuControls::ResolveModifierArt(
            a_inventoryMenu,
            Settings::GetSingleton()->GetFingerSelectModifierKey(),
            Settings::GetSingleton()->GetFingerSelectModifierButton()
        );
    }

    [[nodiscard]] std::optional<std::vector<VanillaItemMenuControls::ButtonArt>> BuildVanillaInventoryShiftedButtonArt(
        const RE::GFxValue& a_inventoryMenu,
        const std::vector<VanillaItemMenuControls::ButtonArt>& a_preservedButtonArt
    ) {
        if (a_preservedButtonArt.size() < kVanillaInventoryShiftedButtonCount - 1) {
            return std::nullopt;
        }

        auto currentEquipArt = VanillaItemMenuControls::ReadFixedSlotButtonArt(a_inventoryMenu, 0, 1);
        if (!currentEquipArt || currentEquipArt->empty()) {
            return std::nullopt;
        }

        std::vector<VanillaItemMenuControls::ButtonArt> result;
        result.reserve(kVanillaInventoryShiftedButtonCount);
        result.push_back(currentEquipArt->front());
        result.push_back(a_preservedButtonArt[0]);
        result.push_back(a_preservedButtonArt[1]);
        return result;
    }

    [[nodiscard]] bool TryEnableVanillaExtendedInventoryData(RE::InventoryMenu& a_inventoryMenu) {
        auto* movie = a_inventoryMenu.uiMovie.get();
        if (!movie
            || movie->IsAvailable(kSkyUIUpdateBottomBarPath)
            || !movie->IsAvailable(kVanillaUpdateBottomBarPath)) {
            return false;
        }

        std::array<RE::GFxValue, 1> args;
        args[0].SetBoolean(true);
        return movie->Invoke(kSkseExtendDataPath, nullptr, args.data(), static_cast<std::uint32_t>(args.size()));
    }

    void AddPendingInventoryUpdateRefresh(const std::uint32_t a_work) {
        pendingInventoryUpdateRefresh.fetch_or(a_work);
    }

    [[nodiscard]] bool RequestInventoryListUpdate(
        RE::InventoryMenu& a_inventoryMenu,
        const std::uint32_t a_postUpdateWork
    ) {
        auto& runtimeData = REL::RelocateMember<RE::InventoryMenu::RUNTIME_DATA>(
            std::addressof(a_inventoryMenu),
            kRuntimeDataOffset,
            kVRRuntimeDataOffset
        );
        auto* itemList = runtimeData.itemList;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (itemList && player) {
            AddPendingInventoryUpdateRefresh(a_postUpdateWork);
            // ItemList::Update queues kInventoryUpdate on SE/AE/VR, so the rows are not ready here.
            itemList->Update(player);
            return true;
        }

        return false;
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
                && IsSelectedFingerSelectHintRow(*a_params.movie, kSkyUISelectedEntryPath)
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
            const auto useRingEquipArt = IsSelectedRingEquipHintRow(*a_params.movie, kVanillaSelectedEntryPath);

            RE::GFxValue originalAltButtonArt;
            auto restoreAltButtonArt = false;

            if (useRingEquipArt && a_params.thisPtr && Scaleform::CanReadMembers(*a_params.thisPtr)) {
                RE::GFxValue equipButtonArt;
                if (a_params.thisPtr->GetMember(kVanillaAltButtonArt, std::addressof(originalAltButtonArt))
                    && originalAltButtonArt.IsObject()
                    && a_params.thisPtr->GetMember(kVanillaEquipButtonArt, std::addressof(equipButtonArt))
                    && equipButtonArt.IsObject()) {
                    restoreAltButtonArt = a_params.thisPtr->SetMember(kVanillaAltButtonArt, equipButtonArt);
                }
            }

            if (a_params.thisPtr && preservedButtonArt_) {
                static_cast<void>(VanillaItemMenuControls::TrySetFixedSlotButtonArt(
                    *a_params.movie,
                    *a_params.thisPtr,
                    kVanillaInventoryPreservedButtonArtFirstIndex,
                    *preservedButtonArt_
                ));
            }

            originalFunction_.Invoke(
                "call",
                a_params.retVal,
                a_params.argsWithThisRef,
                static_cast<std::size_t>(a_params.argCount) + 1
            );

            if (restoreAltButtonArt) {
                static_cast<void>(a_params.thisPtr->SetMember(kVanillaAltButtonArt, originalAltButtonArt));
            }

            if (a_params.thisPtr) {
                if (!preservedButtonArt_) {
                    preservedButtonArt_ = VanillaItemMenuControls::ReadFixedSlotButtonArt(
                        *a_params.thisPtr,
                        kVanillaInventoryPreservedButtonArtFirstIndex,
                        kVanillaInventoryPreservedButtonArtCount
                    );
                }

                if (!preservedButtonArt_) {
                    return;
                }

                const auto fingerArt = GetVisibleVanillaFingerSelectHintArt(*a_params.movie, *a_params.thisPtr);
                if (fingerArt) {
                    const auto shiftedButtonArt = BuildVanillaInventoryShiftedButtonArt(
                        *a_params.thisPtr,
                        *preservedButtonArt_
                    );
                    if (!shiftedButtonArt) {
                        return;
                    }

                    const auto label = Localization::Translate(kInventoryFingerHintKey, "Finger");
                    static_cast<void>(VanillaItemMenuControls::TryPrependFixedSlotButton(
                        *a_params.movie,
                        *a_params.thisPtr,
                        *fingerArt,
                        label,
                        *shiftedButtonArt
                    ));
                }
            }
        }

    private:
        RE::GFxValue originalFunction_;
        std::optional<std::vector<VanillaItemMenuControls::ButtonArt>> preservedButtonArt_;
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

    [[nodiscard]] bool InstallButtonHintFunctionPatches(RE::InventoryMenu& a_inventoryMenu) {
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
        if (movie->IsAvailable(kSkyUIUpdateBottomBarPath)) {
            lastOpenedMenuFlavor.store(InventoryMenuFlavor::kSkyUI);
        }

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
        if (!movie->IsAvailable(kSkyUIUpdateBottomBarPath) && movie->IsAvailable(kVanillaUpdateBottomBarPath)) {
            lastOpenedMenuFlavor.store(InventoryMenuFlavor::kVanilla);
        }

        return installed;
    }

    void RefreshButtonHints(RE::InventoryMenu& a_inventoryMenu) {
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

    void InvalidateInventoryListData(RE::InventoryMenu& a_menu) {
        auto* movie = a_menu.uiMovie.get();
        if (!movie) {
            return;
        }

        if (movie->IsAvailable(kSkyUIInvalidateListDataPath)) {
            static_cast<void>(movie->Invoke(kSkyUIInvalidateListDataPath, nullptr, nullptr, 0));
            return;
        }

        if (movie->IsAvailable(kVanillaInvalidateListDataPath)) {
            static_cast<void>(movie->Invoke(kVanillaInvalidateListDataPath, nullptr, nullptr, 0));
        }
    }

    [[nodiscard]] std::optional<bool> RestampInventoryRingRows(RE::InventoryMenu& a_inventoryMenu) {
        auto& runtimeData = REL::RelocateMember<RE::InventoryMenu::RUNTIME_DATA>(
            std::addressof(a_inventoryMenu),
            kRuntimeDataOffset,
            kVRRuntimeDataOffset
        );
        auto* itemList = runtimeData.itemList;
        if (!itemList || !itemList->entryList.IsArray()) {
            return std::nullopt;
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

        return changedEntryRows > 0;
    }

    void ApplyImmediateInventoryRowRefresh(
        RE::InventoryMenu& a_inventoryMenu,
        const bool a_buttonHintPatchesInstalled
    ) {
        const auto rowsChanged = RestampInventoryRingRows(a_inventoryMenu);
        if (!rowsChanged) {
            if (a_buttonHintPatchesInstalled) {
                RefreshButtonHints(a_inventoryMenu);
            }
            return;
        }

        if (*rowsChanged) {
            InvalidateInventoryListData(a_inventoryMenu);
        }

        if (a_buttonHintPatchesInstalled || *rowsChanged) {
            RefreshButtonHints(a_inventoryMenu);
        }
    }

    void InitializeOpenInventoryMenu(RE::InventoryMenu& a_inventoryMenu) {
        const auto buttonHintPatchesInstalled = InstallButtonHintFunctionPatches(a_inventoryMenu);
        if (TryEnableVanillaExtendedInventoryData(a_inventoryMenu)
            && RequestInventoryListUpdate(a_inventoryMenu, kPendingRestampRingRows | kPendingRefreshButtonHints)) {
            return;
        }

        ApplyImmediateInventoryRowRefresh(a_inventoryMenu, buttonHintPatchesInstalled);
    }

}

bool LastOpenedMenuUsesVanillaBottomBar() {
    return lastOpenedMenuFlavor.load() == InventoryMenuFlavor::kVanilla;
}

void OnClosed() {
    pendingInventoryUpdateRefresh.store(0);
}

void OnShown(RE::InventoryMenu& a_inventoryMenu) {
    InitializeOpenInventoryMenu(a_inventoryMenu);
}

void OnInventoryUpdateProcessed(RE::InventoryMenu& a_inventoryMenu) {
    const auto work = pendingInventoryUpdateRefresh.exchange(0);
    if (work == 0) {
        return;
    }

    bool rowsChanged = false;
    if ((work & kPendingRestampRingRows) != 0) {
        rowsChanged = RestampInventoryRingRows(a_inventoryMenu).value_or(false);
    }

    if (rowsChanged) {
        InvalidateInventoryListData(a_inventoryMenu);
    }

    if (rowsChanged || (work & kPendingRefreshButtonHints) != 0) {
        RefreshButtonHints(a_inventoryMenu);
    }
}

bool TryRefreshOpenMenuRows() {
    auto* inventoryMenu = GetOpenInventoryMenu();
    if (!inventoryMenu) {
        return false;
    }

    static_cast<void>(TryEnableVanillaExtendedInventoryData(*inventoryMenu));
    static_cast<void>(RequestInventoryListUpdate(*inventoryMenu, kPendingRestampRingRows | kPendingRefreshButtonHints));
    return true;
}

bool TryRefreshOpenMenuRowsForRing(RE::Actor& a_actor, const RE::TESObjectARMO& a_ring) {
    if (!GetOpenInventoryMenu()) {
        return false;
    }

    AddPendingInventoryUpdateRefresh(kPendingRestampRingRows | kPendingRefreshButtonHints);
    RE::SendUIMessage::SendInventoryUpdateMessage(std::addressof(a_actor), std::addressof(a_ring));
    return true;
}
}
