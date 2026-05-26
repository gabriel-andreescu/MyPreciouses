#include "FingerSelectMenu.h"

#include <algorithm>
#include <array>
#include <format>
#include <mutex>
#include <utility>

namespace FingerSelectMenu {
namespace {
    constexpr auto kMovieFile = "pv_fingerselect.swf";
    constexpr auto kClipName = "LHRSFingerSelect_mc";
    constexpr auto kClipPath = "_root.LHRSFingerSelect_mc";
    constexpr auto kClipDepth = 1000000;
    constexpr auto kPlatformPC = 0;
    constexpr auto kPlatform360 = 2;
    constexpr auto kPlatformPS3 = 3;
    constexpr bool kSwapPS3Buttons = false;

    struct Session {
        Data::HostMenu hostMenu;
        RE::BSFixedString menuName;
        RE::GPtr<RE::IMenu> menu;
        RE::GPtr<RE::GFxMovieView> movie;
    };

    class EmbeddedHandler;
    [[nodiscard]] RE::FxDelegateHandler* GetHandler();

    std::mutex g_lock;
    std::optional<Data> g_data;
    std::optional<Session> g_session;

    [[nodiscard]] bool IsPCInputDevice(const RE::INPUT_DEVICE a_device) {
        return a_device
               == RE::INPUT_DEVICE::kKeyboard
               || a_device
               == RE::INPUT_DEVICE::kMouse
               || a_device
               == RE::INPUT_DEVICES::VirtualKeyboard();
    }

    [[nodiscard]] std::int32_t GetGamepadPlatform() {
        const auto* controlMap = RE::ControlMap::GetSingleton();
        if (controlMap && controlMap->GetGamePadType() == RE::PC_GAMEPAD_TYPE::kOrbis) {
            return kPlatformPS3;
        }

        return kPlatform360;
    }

    [[nodiscard]] std::int32_t GetPlatform(const RE::INPUT_DEVICE a_preferredDevice) {
        if (IsPCInputDevice(a_preferredDevice)) {
            return kPlatformPC;
        }

        if (a_preferredDevice == RE::INPUT_DEVICE::kGamepad) {
            return GetGamepadPlatform();
        }

        auto* input = RE::BSInputDeviceManager::GetSingleton();
        if (input && input->IsGamepadEnabled()) {
            return GetGamepadPlatform();
        }

        return kPlatformPC;
    }

    [[nodiscard]] std::size_t ClampIndex(const std::size_t a_index) {
        return std::min(a_index, kRowCount - 1);
    }

    [[nodiscard]] std::optional<Data> GetData() {
        std::scoped_lock lock(g_lock);
        return g_data;
    }

    [[nodiscard]] std::optional<Session> GetSession() {
        std::scoped_lock lock(g_lock);
        return g_session;
    }

    [[nodiscard]] RE::BSFixedString GetHostMenuName(const Data::HostMenu a_hostMenu) {
        switch (a_hostMenu) {
            case Data::HostMenu::kInventory: return RE::InventoryMenu::MENU_NAME;
            case Data::HostMenu::kFavorites: return RE::FavoritesMenu::MENU_NAME;
            default:                         return RE::InventoryMenu::MENU_NAME;
        }
    }

    [[nodiscard]] RE::GPtr<RE::IMenu> GetHostMenu(const Data::HostMenu a_hostMenu) {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            return nullptr;
        }

        switch (a_hostMenu) {
            case Data::HostMenu::kInventory: return ui->GetMenu(RE::InventoryMenu::MENU_NAME);
            case Data::HostMenu::kFavorites: return ui->GetMenu(RE::FavoritesMenu::MENU_NAME);
            default:                         return nullptr;
        }
    }

    void UpdateSelectedIndex(const std::size_t a_index) {
        std::scoped_lock lock(g_lock);
        if (g_data) {
            g_data->selectedIndex = ClampIndex(a_index);
        }
    }

    [[nodiscard]] std::optional<Data> TakeData() {
        std::scoped_lock lock(g_lock);
        auto data = std::move(g_data);
        g_data = std::nullopt;
        return data;
    }

    [[nodiscard]] bool SetBool(RE::GFxMovieView& a_movie, const char* a_path, const bool a_value) {
        RE::GFxValue value;
        value.SetBoolean(a_value);
        return a_movie.SetVariable(a_path, value, RE::GFxMovie::SetVarType::kNormal);
    }

    void SetHostControlsEnabled(RE::GFxMovieView& a_movie, const Data::HostMenu a_hostMenu, const bool a_enabled) {
        const auto disabled = !a_enabled;

        switch (a_hostMenu) {
            case Data::HostMenu::kInventory:
                static_cast<void>(SetBool(a_movie, "_root.Menu_mc.inventoryLists.itemList.disableInput", disabled));
                static_cast<void>(SetBool(a_movie, "_root.Menu_mc.inventoryLists.itemList.disableSelection", disabled));
                static_cast<void>(SetBool(a_movie, "_root.Menu_mc.inventoryLists.categoryList.disableInput", disabled));
                static_cast<void>(
                    SetBool(a_movie, "_root.Menu_mc.inventoryLists.categoryList.disableSelection", disabled)
                );
                static_cast<void>(SetBool(a_movie, "_root.Menu_mc.inventoryLists.searchWidget.isDisabled", disabled));
                static_cast<void>(
                    SetBool(a_movie, "_root.Menu_mc.inventoryLists.columnSelectButton.disabled", disabled)
                );
                break;
            case Data::HostMenu::kFavorites:
                static_cast<void>(SetBool(a_movie, "_root.MenuHolder.Menu_mc.itemList.disableInput", disabled));
                static_cast<void>(SetBool(a_movie, "_root.MenuHolder.Menu_mc.itemList.disableSelection", disabled));
                static_cast<void>(SetBool(a_movie, "_root.MenuHolder.Menu_mc.btnAll.disabled", disabled));
                static_cast<void>(SetBool(a_movie, "_root.MenuHolder.Menu_mc.btnGear.disabled", disabled));
                static_cast<void>(SetBool(a_movie, "_root.MenuHolder.Menu_mc.btnAid.disabled", disabled));
                static_cast<void>(SetBool(a_movie, "_root.MenuHolder.Menu_mc.btnMagic.disabled", disabled));
                for (auto index = 1; index <= 8; ++index) {
                    const auto path = std::format(
                        "_root.MenuHolder.Menu_mc.groupButtonFader.groupButtonHolder.btnGroup{}.disabled",
                        index
                    );
                    static_cast<void>(SetBool(a_movie, path.c_str(), disabled));
                }
                break;
        }
    }

    void SetInventoryItem3DVisibility(const RE::IMenu& a_menu, RE::GFxMovieView& a_movie, const bool a_visible) {
        if (!a_menu.fxDelegate) {
            return;
        }

        std::array<RE::GFxValue, 2> args;
        args[0].SetNumber(0.0);
        args[1].SetBoolean(a_visible);
        a_menu.fxDelegate->Callback(&a_movie, "UpdateItem3D", args.data(), static_cast<std::uint32_t>(args.size()));
    }

    [[nodiscard]] bool Invoke(
        RE::GFxMovieView& a_movie,
        const char* a_path,
        RE::GFxValue* a_args,
        const std::uint32_t a_argCount
    ) {
        return a_movie.Invoke(a_path, nullptr, a_args, a_argCount);
    }

    [[nodiscard]] bool InvokeClip(
        RE::GFxMovieView& a_movie,
        const char* a_functionName,
        RE::GFxValue* a_args,
        const std::uint32_t a_argCount
    ) {
        const auto path = std::format("{}.{}", kClipPath, a_functionName);
        return Invoke(a_movie, path.c_str(), a_args, a_argCount);
    }

    [[nodiscard]] bool ApplyPlatform(RE::GFxMovieView& a_movie, const RE::INPUT_DEVICE a_preferredDevice) {
        const auto platform = GetPlatform(a_preferredDevice);

        std::array<RE::GFxValue, 2> args;
        args[0].SetNumber(static_cast<double>(platform));
        args[1].SetBoolean(kSwapPS3Buttons);
        if (!InvokeClip(a_movie, "SetPlatform", args.data(), static_cast<std::uint32_t>(args.size()))) {
            logger::warn("FingerSelectMenu: platform update failed | platform={}", platform);
            return false;
        }

        return true;
    }

    [[nodiscard]] bool ApplyLabels(RE::GFxMovieView& a_movie, const Data& a_data) {
        std::array<RE::GFxValue, 6> args;
        args[0].SetString(a_data.title.c_str());
        args[1].SetString(a_data.ringName.c_str());
        args[2].SetString(a_data.fingerHeader.c_str());
        args[3].SetString(a_data.equippedHeader.c_str());
        args[4].SetString(a_data.equipLabel.c_str());
        args[5].SetString(a_data.cancelLabel.c_str());

        if (!InvokeClip(a_movie, "SetLabels", args.data(), static_cast<std::uint32_t>(args.size()))) {
            logger::warn("FingerSelectMenu: label update failed");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool ApplyRows(RE::GFxMovieView& a_movie, const Data& a_data) {
        auto applied = true;
        for (std::size_t index = 0; index < a_data.rows.size(); ++index) {
            const auto& row = a_data.rows[index];
            std::array<RE::GFxValue, 5> args;
            args[0].SetNumber(static_cast<double>(index));
            args[1].SetNumber(static_cast<double>(ToIndex(row.target)));
            args[2].SetString(row.fingerLabel.c_str());
            args[3].SetString(row.equippedRingLabel.c_str());
            args[4].SetBoolean(row.enabled);

            if (!InvokeClip(a_movie, "SetRow", args.data(), static_cast<std::uint32_t>(args.size()))) {
                logger::warn("FingerSelectMenu: row update failed | index={}", index);
                applied = false;
            }
        }

        return applied;
    }

    [[nodiscard]] bool ApplyCommit(RE::GFxMovieView& a_movie, const Data& a_data) {
        std::array<RE::GFxValue, 1> args;
        args[0].SetNumber(static_cast<double>(ClampIndex(a_data.selectedIndex)));
        if (!InvokeClip(a_movie, "CommitData", args.data(), static_cast<std::uint32_t>(args.size()))) {
            logger::warn("FingerSelectMenu: commit failed | selectedIndex={}", a_data.selectedIndex);
            return false;
        }

        return true;
    }

    [[nodiscard]] bool ApplyData(RE::GFxMovieView& a_movie, const Data& a_data) {
        const auto platform = ApplyPlatform(a_movie, a_data.inputDevice);
        const auto labels = ApplyLabels(a_movie, a_data);
        const auto rows = ApplyRows(a_movie, a_data);
        const auto commit = ApplyCommit(a_movie, a_data);
        return platform && labels && rows && commit;
    }

    void ApplyCurrentData(RE::GFxMovieView& a_movie) {
        if (const auto data = GetData()) {
            static_cast<void>(ApplyData(a_movie, *data));
        }
    }

    [[nodiscard]] std::optional<std::size_t> ReadIndexArgument(const RE::FxDelegateArgs& a_params) {
        if (a_params.GetArgCount() == 0 || !a_params[0].IsNumber()) {
            return std::nullopt;
        }

        const auto rawIndex = static_cast<std::int32_t>(a_params[0].GetNumber());
        if (rawIndex < 0) {
            return std::nullopt;
        }

        return ClampIndex(static_cast<std::size_t>(rawIndex));
    }

    [[nodiscard]] std::optional<RingTarget> ReadTargetArgument(const RE::FxDelegateArgs& a_params) {
        if (a_params.GetArgCount() < 2 || !a_params[1].IsNumber()) {
            return std::nullopt;
        }

        const auto rawTarget = static_cast<std::int32_t>(a_params[1].GetNumber());
        if (rawTarget < 0) {
            return std::nullopt;
        }

        return FromIndex(static_cast<std::uint32_t>(rawTarget));
    }

    [[nodiscard]] std::size_t ResolveResultIndex(
        const Data& a_data,
        const std::optional<std::size_t> a_index,
        const std::optional<RingTarget> a_target
    ) {
        if (a_target) {
            const auto it = std::ranges::find_if(a_data.rows, [&](const Row& a_row) {
                return a_row.target == *a_target;
            });
            if (it != a_data.rows.end()) {
                return static_cast<std::size_t>(std::distance(a_data.rows.begin(), it));
            }
        }

        return ClampIndex(a_index.value_or(a_data.selectedIndex));
    }

    void CompleteCancel() {
        auto data = TakeData();
        if (!data || !data->onResult) {
            return;
        }

        data->onResult(
            Result {
                .action = Result::Action::kCancel,
                .target = std::nullopt,
                .index = ClampIndex(data->selectedIndex),
            }
        );
    }

    void CloseSession(const bool a_restoreHostControls) {
        auto session = std::optional<Session> {};
        {
            std::scoped_lock lock(g_lock);
            session = std::move(g_session);
            g_session = std::nullopt;
        }

        if (!session || !session->movie) {
            return;
        }

        if (a_restoreHostControls) {
            SetHostControlsEnabled(*session->movie, session->hostMenu, true);
            if (session->hostMenu == Data::HostMenu::kInventory && session->menu) {
                SetInventoryItem3DVisibility(*session->menu, *session->movie, true);
            }
            static_cast<void>(InvokeClip(*session->movie, "RestoreFocus", nullptr, 0));
        }

        if (session->menu && session->menu->fxDelegate) {
            session->menu->fxDelegate->UnregisterHandler(GetHandler());
        }

        static_cast<void>(SetBool(*session->movie, std::format("{}._visible", kClipPath).c_str(), false));
        static_cast<void>(Invoke(*session->movie, std::format("{}.removeMovieClip", kClipPath).c_str(), nullptr, 0));
    }

    void CompleteResult(Result a_result) {
        auto data = TakeData();
        CloseSession(true);

        if (!data || !data->onResult) {
            return;
        }

        data->onResult(std::move(a_result));
    }

    class EmbeddedHandler final : public RE::FxDelegateHandler {
    public:
        void Accept(CallbackProcessor* a_processor) override {
            if (!a_processor) {
                return;
            }

            a_processor->Process("PVFingerSelect_LoadMenu", LoadMenuCallback);
            a_processor->Process("PVFingerSelect_SelectionChange", SelectionChangedCallback);
            a_processor->Process("PVFingerSelect_Equip", EquipCallback);
            a_processor->Process("PVFingerSelect_Cancel", CancelCallback);
        }

    private:
        static void LoadMenuCallback(const RE::FxDelegateArgs& a_params) {
            if (auto* movie = a_params.GetMovie()) {
                ApplyCurrentData(*movie);
            }
        }

        static void SelectionChangedCallback(const RE::FxDelegateArgs& a_params) {
            if (const auto index = ReadIndexArgument(a_params)) {
                UpdateSelectedIndex(*index);
            }
        }

        static void EquipCallback(const RE::FxDelegateArgs& a_params) {
            const auto data = GetData();
            if (!data || !data->onResult) {
                return;
            }

            const auto targetArg = ReadTargetArgument(a_params);
            const auto index = ResolveResultIndex(*data, ReadIndexArgument(a_params), targetArg);
            const auto target = targetArg.value_or(data->rows[index].target);
            UpdateSelectedIndex(index);
            CompleteResult(
                Result {
                    .action = Result::Action::kEquip,
                    .target = target,
                    .index = index,
                }
            );
        }

        static void CancelCallback([[maybe_unused]] const RE::FxDelegateArgs& a_params) {
            Cancel();
        }
    };

    [[nodiscard]] RE::FxDelegateHandler* GetHandler() {
        static auto handler = RE::make_gptr<EmbeddedHandler>();
        return static_cast<RE::FxDelegateHandler*>(handler.get());
    }

    [[nodiscard]] bool LoadEmbeddedMovie(RE::GFxMovieView& a_movie) {
        static_cast<void>(Invoke(a_movie, std::format("{}.removeMovieClip", kClipPath).c_str(), nullptr, 0));

        std::array<RE::GFxValue, 2> createArgs;
        createArgs[0].SetString(kClipName);
        createArgs[1].SetNumber(static_cast<double>(kClipDepth));
        if (!Invoke(
                a_movie,
                "_root.createEmptyMovieClip",
                createArgs.data(),
                static_cast<std::uint32_t>(createArgs.size())
            )) {
            logger::warn("FingerSelectMenu: open failed | reason=viewCreationFailed");
            return false;
        }

        static_cast<void>(SetBool(a_movie, std::format("{}._lockroot", kClipPath).c_str(), true));

        std::array<RE::GFxValue, 1> loadArgs;
        loadArgs[0].SetString(kMovieFile);
        if (!Invoke(
                a_movie,
                std::format("{}.loadMovie", kClipPath).c_str(),
                loadArgs.data(),
                static_cast<std::uint32_t>(loadArgs.size())
            )) {
            logger::warn("FingerSelectMenu: open failed | reason=viewLoadFailed");
            return false;
        }

        return true;
    }
}

bool Show(Data a_data) {
    if (!a_data.onResult) {
        return false;
    }

    a_data.selectedIndex = ClampIndex(a_data.selectedIndex);
    {
        std::scoped_lock lock(g_lock);
        if (g_data || g_session) {
            return false;
        }

        g_data = std::move(a_data);
    }

    auto data = GetData();
    auto menu = data ? GetHostMenu(data->hostMenu) : nullptr;
    auto movie = menu ? menu->uiMovie : nullptr;
    auto* handler = GetHandler();
    if (data && menu && movie && menu->fxDelegate && handler) {
        menu->fxDelegate->RegisterHandler(handler);
        SetHostControlsEnabled(*movie, data->hostMenu, false);
        if (data->hostMenu == Data::HostMenu::kInventory) {
            SetInventoryItem3DVisibility(*menu, *movie, false);
        }
        {
            std::scoped_lock lock(g_lock);
            g_session = Session {
                .hostMenu = data->hostMenu,
                .menuName = GetHostMenuName(data->hostMenu),
                .menu = menu,
                .movie = movie,
            };
        }
        if (LoadEmbeddedMovie(*movie)) {
            return true;
        }

        CloseSession(true);
    } else {
        logger::warn("FingerSelectMenu: open failed | reason=menuUnavailable");
    }

    {
        std::scoped_lock lock(g_lock);
        g_data = std::nullopt;
        g_session = std::nullopt;
    }
    return false;
}

bool IsOpen() {
    std::scoped_lock lock(g_lock);
    return g_session.has_value();
}

bool IsPendingOrOpen() {
    std::scoped_lock lock(g_lock);
    return g_data.has_value() || g_session.has_value();
}

void OnMenuClose(const RE::BSFixedString& a_menuName) {
    const auto session = GetSession();
    if (session && a_menuName == session->menuName) {
        CloseSession(false);
        CompleteCancel();
    }
}

void Cancel() {
    auto data = GetData();
    if (data && data->onResult) {
        CompleteResult(
            Result {
                .action = Result::Action::kCancel,
                .target = std::nullopt,
                .index = ClampIndex(data->selectedIndex),
            }
        );
        return;
    }

    CloseSession(true);
    {
        std::scoped_lock lock(g_lock);
        g_data = std::nullopt;
    }
}
}
