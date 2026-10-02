#include "Compatibility/SkyUI/FavoritesMenu.h"

#include <RE/Skyrim.h> // IWYU pragma: keep

#include "Core/Target.h"
#include "Inventory.h"
#include "UI/FavoritesMenu.h"
#include "UI/Scaleform.h"

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

namespace Compatibility::SkyUI::FavoritesMenu {
namespace {
    namespace Groups = Equipment::FavoriteGroups;

    constexpr auto kOriginalSave = "_myPreciousesSaveEquipState";

    std::mutex g_captureLock;
    std::array<std::optional<std::vector<Groups::Row>>, Groups::kGroupCount> g_captures;

    [[nodiscard]] std::optional<Groups::Row> ReadGroupRow(RE::FavoritesMenu& a_menu, const RE::GFxValue& a_row) {
        auto* entry = UI::FavoritesMenu::GetRowEntry(a_menu, a_row);
        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto itemID = UI::Scaleform::ReadIntMember(a_row, "itemId");
        if (!entry || !player || !itemID) {
            return std::nullopt;
        }
        auto source = Inventory::ResolveEntryRingSource(*player, *entry, Inventory::EntryResolveScope::kMenuRow);
        if (!source) {
            return std::nullopt;
        }
        return Groups::Row {
            .member = {.formID = source->ring->GetFormID(), .itemID = *itemID},
            .sources = std::move(source->rowSources),
        };
    }

    class SaveGroupHandler final : public RE::GFxFunctionHandler {
        void Call(Params& a_params) override {
            auto* menu = UI::FavoritesMenu::GetOpenMenu();
            RE::GFxValue list;
            RE::GFxValue entries;
            if (!menu
                || !UI::FavoritesMenu::GetItemList(*menu, list)
                || !list.GetMember("entryList", std::addressof(entries))
                || !entries.IsArray()) {
                a_params.thisPtr->Invoke(kOriginalSave);
                return;
            }
            std::vector<Groups::Row> rows;
            std::vector<std::pair<RE::GFxValue, RE::GFxValue>> states;
            for (std::uint32_t index = 0; index < entries.GetArraySize(); ++index) {
                RE::GFxValue row;
                if (!entries.GetElement(index, std::addressof(row))) {
                    continue;
                }
                if (auto source = ReadGroupRow(*menu, row)) {
                    rows.push_back(std::move(*source));
                    RE::GFxValue state;
                    row.GetMember("equipState", std::addressof(state));
                    states.emplace_back(row, state);
                    // SkyUI's scan reserves these values for weapons and spells.
                    row.SetMember("equipState", RE::GFxValue(0));
                }
            }
            const auto group = UI::Scaleform::ReadIntMember(*a_params.thisPtr, "_groupIndex");
            if (group
                && *group >= 0
                && static_cast<std::size_t>(*group) < g_captures.size()
                && UI::Scaleform::ReadBoolMember(*a_params.thisPtr, "_groupButtonFocused").value_or(false)) {
                std::scoped_lock const lock(g_captureLock);
                g_captures[static_cast<std::size_t>(*group)] = std::move(rows);
            }
            a_params.thisPtr->Invoke(kOriginalSave);
            for (auto& [row, state] : states) {
                row.SetMember("equipState", state);
            }
        }
    };

    void StampGroupFlags(RE::GFxValue& a_row, const Groups::Groups& a_groups) {
        const auto formID = UI::Scaleform::ReadUInt32Member(a_row, "formId");
        const auto itemID = UI::Scaleform::ReadIntMember(a_row, "itemId");
        if (!formID || !itemID || !Inventory::AsRing(RE::TESForm::LookupByID(*formID))) {
            return;
        }
        auto left = UI::Scaleform::ReadUInt32Member(a_row, "offHandFlag").value_or(0);
        auto right = UI::Scaleform::ReadUInt32Member(a_row, "mainHandFlag").value_or(0);
        for (std::size_t group = 0; group < a_groups.size(); ++group) {
            const auto& saved = a_groups[group];
            if (!saved) {
                continue;
            }
            const auto bit = 1U << group;
            left &= ~bit;
            right &= ~bit;
            const auto& layout = *saved;
            for (const auto target : Core::kAllTargets) {
                const auto index = Core::ToIndex(target);
                if (layout.assignments.byTarget[index].source.sourceFormID == *formID
                    && layout.itemIDs[index] == *itemID) {
                    (target.hand == Core::Hand::kLeft ? left : right) |= bit;
                }
            }
        }
        a_row.SetMember("offHandFlag", RE::GFxValue(left));
        a_row.SetMember("mainHandFlag", RE::GFxValue(right));
    }

    class GroupFlagsHandler final : public RE::GFxFunctionHandler {
        void Call(Params& a_params) override {
            RE::GFxValue entries;
            if (a_params.argCount != 1
                || !a_params.args->GetMember("entryList", std::addressof(entries))
                || !entries.IsArray()) {
                return;
            }
            const auto groups = Groups::GetAll();
            for (std::uint32_t index = 0; index < entries.GetArraySize(); ++index) {
                RE::GFxValue row;
                if (entries.GetElement(index, std::addressof(row))) {
                    StampGroupFlags(row, groups);
                }
            }
        }
    };
}

void Attach(RE::FavoritesMenu& a_menu, RE::GFxValue& a_itemList) {
    auto& root = a_menu.GetRuntimeData().root;
    RE::GFxValue original;
    RE::GFxValue addProcessor;
    if (root.HasMember(kOriginalSave)
        || !root.GetMember("startSaveEquipState", std::addressof(original))
        || !original.IsObject()
        || !a_itemList.GetMember("addDataProcessor", std::addressof(addProcessor))
        || !addProcessor.IsObject()) {
        return;
    }
    auto const saveHandler = RE::make_gptr<SaveGroupHandler>();
    auto const flagsHandler = RE::make_gptr<GroupFlagsHandler>();
    RE::GFxValue saveFunction;
    RE::GFxValue processor;
    RE::GFxValue flagsFunction;
    a_menu.uiMovie->CreateFunction(std::addressof(saveFunction), saveHandler.get());
    a_menu.uiMovie->CreateObject(std::addressof(processor));
    a_menu.uiMovie->CreateFunction(std::addressof(flagsFunction), flagsHandler.get());
    processor.SetMember("processList", flagsFunction);
    a_itemList.Invoke("addDataProcessor", nullptr, std::addressof(processor), 1);
    root.SetMember(kOriginalSave, original);
    root.SetMember("startSaveEquipState", saveFunction);
}

std::optional<std::vector<Groups::Row>> TakeCapture(const std::size_t a_group) {
    std::scoped_lock const lock(g_captureLock);
    return std::exchange(g_captures[a_group], std::nullopt);
}

void Revert() {
    std::scoped_lock const lock(g_captureLock);
    g_captures = {};
}
}
