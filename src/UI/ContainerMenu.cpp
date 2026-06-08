#include "UI/ContainerMenu.h"

#include "UI/RingItemRows.h"
#include "UI/Scaleform.h"

#include <RE/C/ContainerMenu.h>
#include <RE/G/GFxFunctionHandler.h>
#include <RE/I/ItemList.h>
#include <RE/M/Misc.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace UI::ContainerMenu {
namespace {
    constexpr std::ptrdiff_t kRuntimeDataOffset = 0x30;
    constexpr std::ptrdiff_t kVRRuntimeDataOffset = 0x50;

    constexpr auto kScaleformShowItemsListPatched = "lhrsContainerShowItemsListPatched";
    constexpr auto kSkyUIInventoryListsPath = "_root.Menu_mc.inventoryLists";
    constexpr auto kSkyUIContainerActiveSegmentPath = "_root.Menu_mc.inventoryLists.categoryList.activeSegment";
    constexpr auto kSkyUISelectedCategoryFlagPath = "_root.Menu_mc.inventoryLists.categoryList.selectedEntry.flag";
    constexpr auto kSkyUIRequestUpdatePath = "_root.Menu_mc.inventoryLists.itemList.requestUpdate";
    constexpr auto kVanillaInventoryListsPath = "_root.Menu_mc.InventoryLists_mc";
    constexpr auto kVanillaContainerSelectedCategoryPath = "_root.Menu_mc.iSelectedCategory";
    constexpr auto kVanillaContainerDividerIndexPath = "_root.Menu_mc.InventoryLists_mc.CategoriesList.dividerIndex";
    constexpr auto
        kVanillaSelectedCategoryFlagPath = "_root.Menu_mc.InventoryLists_mc.CategoriesList.selectedEntry.flag";
    constexpr auto kVanillaUpdateListPath = "_root.Menu_mc.InventoryLists_mc.ItemsList.UpdateList";
    constexpr auto kScaleformFilterFlag = "filterFlag";
    constexpr auto kContainerSideSegment = 0;

    [[nodiscard]] RE::ContainerMenu* GetOpenContainerMenu() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui || !ui->IsMenuOpen(RE::ContainerMenu::MENU_NAME)) {
            return nullptr;
        }

        auto containerMenu = ui->GetMenu<RE::ContainerMenu>();
        return containerMenu.get();
    }

    [[nodiscard]] std::optional<int> ReadMovieIntVariable(RE::GFxMovieView& a_view, const char* a_path) {
        RE::GFxValue value;
        if (!a_view.GetVariable(std::addressof(value), a_path) || !value.IsNumber()) {
            return std::nullopt;
        }

        const auto number = value.GetNumber();
        if (!std::isfinite(number) || std::trunc(number) != number) {
            return std::nullopt;
        }

        constexpr auto minimum = static_cast<double>(std::numeric_limits<int>::lowest());
        constexpr auto maximum = static_cast<double>(std::numeric_limits<int>::max());
        if (number < minimum || number > maximum) {
            return std::nullopt;
        }

        return static_cast<int>(number);
    }

    [[nodiscard]] std::optional<std::uint32_t> ReadMovieUInt32Variable(RE::GFxMovieView& a_view, const char* a_path) {
        RE::GFxValue value;
        if (!a_view.GetVariable(std::addressof(value), a_path) || !value.IsNumber()) {
            return std::nullopt;
        }

        const auto number = value.GetNumber();
        if (!std::isfinite(number) || std::trunc(number) != number) {
            return std::nullopt;
        }

        constexpr auto maximum = static_cast<double>(std::numeric_limits<std::uint32_t>::max());
        if (number < 0.0 || number > maximum) {
            return std::nullopt;
        }

        return static_cast<std::uint32_t>(number);
    }

    [[nodiscard]] std::optional<bool> IsViewingContainerSide(RE::GFxMovieView& a_view) {
        if (const auto activeSegment = ReadMovieIntVariable(a_view, kSkyUIContainerActiveSegmentPath)) {
            return *activeSegment == kContainerSideSegment;
        }

        const auto selectedCategory = ReadMovieIntVariable(a_view, kVanillaContainerSelectedCategoryPath);
        const auto dividerIndex = ReadMovieIntVariable(a_view, kVanillaContainerDividerIndexPath);
        if (!selectedCategory || !dividerIndex) {
            return std::nullopt;
        }

        return *selectedCategory < *dividerIndex;
    }

    [[nodiscard]] std::optional<std::uint32_t> GetSelectedCategoryFlag(RE::GFxMovieView& a_view) {
        if (const auto flag = ReadMovieUInt32Variable(a_view, kSkyUISelectedCategoryFlagPath)) {
            return flag;
        }

        if (const auto flag = ReadMovieUInt32Variable(a_view, kVanillaSelectedCategoryFlagPath)) {
            return flag;
        }

        return std::nullopt;
    }

    [[nodiscard]] bool EntryMatchesVisibleCategory(
        const RE::GFxValue& a_entryObject,
        const std::uint32_t a_categoryFlag
    ) {
        const auto entryFilter = Scaleform::ReadUInt32Member(a_entryObject, kScaleformFilterFlag);
        return entryFilter && (*entryFilter & a_categoryFlag) != 0;
    }

    [[nodiscard]] std::optional<Core::ActorKey> ResolveTargetActorKey() {
        if (RE::ContainerMenu::GetContainerMode() != RE::ContainerMenu::ContainerMode::kNPCMode) {
            return std::nullopt;
        }

        const auto targetHandle = RE::ContainerMenu::GetTargetRefHandle();
        RE::NiPointer<RE::TESObjectREFR> targetPtr;
        if (!RE::LookupReferenceByHandle(targetHandle, targetPtr)) {
            return std::nullopt;
        }

        auto* target = targetPtr.get();
        auto* actor = target ? target->As<RE::Actor>() : nullptr;
        if (!actor || actor->IsPlayerRef() || !actor->IsPlayerTeammate()) {
            return std::nullopt;
        }

        return Core::MakeActorKey(*actor);
    }

    void RequestVisibleListRedraw(RE::GFxMovie& a_view) {
        if (a_view.IsAvailable(kSkyUIRequestUpdatePath)) {
            static_cast<void>(a_view.Invoke(kSkyUIRequestUpdatePath, nullptr, nullptr, 0));
            return;
        }

        if (a_view.IsAvailable(kVanillaUpdateListPath)) {
            static_cast<void>(a_view.Invoke(kVanillaUpdateListPath, nullptr, nullptr, 0));
        }
    }

    [[nodiscard]] bool RestampVisibleRingRows(RE::ContainerMenu& a_containerMenu) {
        auto* movie = a_containerMenu.uiMovie.get();
        auto& runtimeData = REL::RelocateMember<RE::ContainerMenu::RUNTIME_DATA>(
            std::addressof(a_containerMenu),
            kRuntimeDataOffset,
            kVRRuntimeDataOffset
        );
        auto* itemList = runtimeData.itemList;
        if (!movie || !itemList || !itemList->entryList.IsArray()) {
            return false;
        }

        const auto visibleActor = ResolveVisibleActorKey(movie);
        const auto categoryFlag = GetSelectedCategoryFlag(*movie);
        if (!categoryFlag) {
            return false;
        }

        auto changed = false;
        const auto entryCount = itemList->entryList.GetArraySize();
        const auto itemCount = static_cast<std::uint32_t>(itemList->items.size());
        for (std::uint32_t index = 0; index < entryCount && index < itemCount; ++index) {
            RE::GFxValue entryObject;
            if (!itemList->entryList.GetElement(index, std::addressof(entryObject))
                || !Scaleform::CanReadMembers(entryObject)
                || !EntryMatchesVisibleCategory(entryObject, *categoryFlag)) {
                continue;
            }

            const auto result = [&]() {
                if (!visibleActor) {
                    return RingItemRows::ClearRingEntry(entryObject);
                }

                const auto* item = itemList->items[index];
                auto* entry = item ? item->data.objDesc : nullptr;
                return entry ? RingItemRows::StampRingEntry(entryObject, *entry, *visibleActor, true)
                             : RingItemRows::ClearRingEntry(entryObject);
            }();

            if (result == RingItemRows::RowStampResult::kChanged) {
                changed = itemList->entryList.SetElement(index, entryObject) || changed;
            }
        }

        return changed;
    }

    class ShowItemsListHandler final : public RE::GFxFunctionHandler {
    public:
        explicit ShowItemsListHandler(const RE::GFxValue& a_originalFunction)
            : originalFunction_(a_originalFunction) {}

        void Call(Params& a_params) override {
            originalFunction_.Invoke(
                "call",
                a_params.retVal,
                a_params.argsWithThisRef,
                static_cast<std::size_t>(a_params.argCount) + 1
            );

            if (TryRefreshOpenMenuRows() && a_params.movie) {
                RequestVisibleListRedraw(*a_params.movie);
            }
        }

    private:
        RE::GFxValue originalFunction_;
    };

    [[nodiscard]] bool InstallShowItemsListPatch(
        RE::GFxMovieView& a_movie,
        const char* a_inventoryListsPath,
        const char* a_memberName
    ) {
        RE::GFxValue inventoryLists;
        if (!a_movie.GetVariable(std::addressof(inventoryLists), a_inventoryListsPath)
            || !Scaleform::CanReadMembers(inventoryLists)
            || Scaleform::ReadBoolMember(inventoryLists, kScaleformShowItemsListPatched).value_or(false)) {
            return false;
        }

        RE::GFxValue originalFunction;
        if (!inventoryLists.GetMember(a_memberName, std::addressof(originalFunction)) || !originalFunction.IsObject()) {
            return false;
        }

        auto handler = RE::make_gptr<ShowItemsListHandler>(originalFunction);
        RE::GFxValue function;
        a_movie.CreateFunction(std::addressof(function), handler.get());
        if (!inventoryLists.SetMember(a_memberName, function)) {
            return false;
        }

        inventoryLists.SetMember(kScaleformShowItemsListPatched, true);
        return true;
    }

    void InstallShowItemsListPatches(RE::ContainerMenu& a_containerMenu) {
        auto* movie = a_containerMenu.uiMovie.get();
        if (!movie) {
            return;
        }

        auto installed = InstallShowItemsListPatch(*movie, kSkyUIInventoryListsPath, "showItemsList");
        installed = InstallShowItemsListPatch(*movie, kVanillaInventoryListsPath, "ShowItemsList") || installed;
        if (installed) {
            logger::info("UI: ContainerMenu ShowItemsList patch installed");
        }
    }

}

bool IsOpenMovie(const RE::GFxMovieView* a_view) {
    const auto* containerMenu = GetOpenContainerMenu();
    return a_view && containerMenu && containerMenu->uiMovie.get() == a_view;
}

std::optional<Core::ActorKey> ResolveVisibleActorKey(RE::GFxMovieView* a_view) {
    if (!IsOpenMovie(a_view)) {
        return std::nullopt;
    }

    const auto viewingContainer = IsViewingContainerSide(*a_view);
    if (!viewingContainer) {
        return std::nullopt;
    }

    if (*viewingContainer) {
        return ResolveTargetActorKey();
    }

    return Core::GetPlayerActorKey();
}

void OnClosed() {}

void OnShown(RE::ContainerMenu& a_containerMenu) {
    InstallShowItemsListPatches(a_containerMenu);
    if (RestampVisibleRingRows(a_containerMenu)) {
        if (auto* movie = a_containerMenu.uiMovie.get()) {
            RequestVisibleListRedraw(*movie);
        }
    }
    stl::add_ui_task([] {
        static_cast<void>(TryRefreshOpenMenuRows());
    });
}

void OnInventoryUpdateProcessed(RE::ContainerMenu& a_containerMenu) {
    if (RestampVisibleRingRows(a_containerMenu)) {
        if (auto* movie = a_containerMenu.uiMovie.get()) {
            RequestVisibleListRedraw(*movie);
        }
    }
}

bool TryRefreshOpenMenuRows() {
    auto* containerMenu = GetOpenContainerMenu();
    if (!containerMenu) {
        return false;
    }

    const auto changed = RestampVisibleRingRows(*containerMenu);
    if (changed) {
        if (auto* movie = containerMenu->uiMovie.get()) {
            RequestVisibleListRedraw(*movie);
        }
    }
    return true;
}
}
