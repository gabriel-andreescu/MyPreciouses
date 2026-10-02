#include "Compatibility/SkyUI/FavoritesManager.h"

#include <RE/Skyrim.h> // IWYU pragma: keep

#include "Compatibility/SkyUI/FavoritesMenu.h"
#include "Equipment/FavoriteGroupActions.h"

#include <memory>
#include <utility>

namespace Compatibility::SkyUI::FavoritesManager {
namespace {
    namespace Groups = Equipment::FavoriteGroups;

    constexpr std::uint32_t kGroupItemCount = 32;
    constexpr std::uint32_t kGroupsPerArray = 4;
    constexpr std::uint32_t kGroupArraySize = kGroupItemCount * kGroupsPerArray;
    constexpr std::int32_t kArmorType = 26;
    constexpr std::uint32_t kUnequipArmorFlag = 1;
    constexpr auto kOutfitMask = "_usedOutfitMask";

    [[nodiscard]] RE::BSScript::Variable& Argument(
        const RE::BSScript::StackFrame& a_frame,
        const std::uint32_t a_index
    ) {
        return a_frame.GetStackFrameVariable(a_index, a_frame.GetPageForFrame());
    }

    [[nodiscard]] RE::BSTSmartPointer<RE::BSScript::Array> ReadArray(
        const RE::BSScript::Object& a_object,
        const char* a_name,
        const std::uint32_t a_size
    ) {
        const auto* variable = a_object.GetVariable(a_name);
        const auto array = variable && variable->IsArray() ? variable->GetArray() : nullptr;
        return array && array->size() == a_size ? array : nullptr;
    }

    [[nodiscard]] bool ReadGroup(const RE::BSScript::Object& a_manager, Call& a_call) {
        const auto first = a_call.group < kGroupsPerArray;
        const auto items = ReadArray(a_manager, first ? "_items1" : "_items2", kGroupArraySize);
        const auto ids = ReadArray(a_manager, first ? "_itemIds1" : "_itemIds2", kGroupArraySize);
        const auto invalid = ReadArray(a_manager, first ? "_itemInvalidFlags1" : "_itemInvalidFlags2", kGroupArraySize);
        const auto flags = ReadArray(a_manager, "_groupFlags", Groups::kGroupCount);
        const auto* outfitMask = a_manager.GetVariable(kOutfitMask);
        if (!items || !ids || !invalid || !flags || !outfitMask || !outfitMask->IsInt()) {
            return false;
        }
        const auto begin = static_cast<std::uint32_t>(a_call.group % kGroupsPerArray) * kGroupItemCount;
        for (auto index = begin; index < begin + kGroupItemCount; ++index) {
            const auto* form = (*items)[index].Unpack<RE::TESForm*>();
            if (form && !(*invalid)[index].GetBool()) {
                a_call.members.push_back({.formID = form->GetFormID(), .itemID = (*ids)[index].GetSInt()});
            }
        }
        const auto groupFlags = static_cast<std::uint32_t>(
            (*flags)[static_cast<std::uint32_t>(a_call.group)].GetSInt()
        );
        a_call.unequipArmor = (groupFlags & kUnequipArmorFlag) != 0;
        return true;
    }

    [[nodiscard]] bool IsGroupUse(const RE::BSScript::StackFrame* a_frame, const RE::BSScript::StackFrame& a_callee) {
        return a_frame != nullptr
               && a_frame->owningFunction->GetName() == "GroupUse"
               && a_frame->self.GetObject() == a_callee.self.GetObject();
    }
}

bool IsManager(const RE::BSScript::IFunction& a_function) {
    static const RE::BSFixedString manager {"SKI_FavoritesManager"};
    return a_function.GetObjectTypeName() == manager;
}

std::optional<Call> ReadCall(const RE::BSScript::IFunction& a_function, const RE::BSScript::StackFrame& a_frame) {
    const auto& name = a_function.GetName();
    const auto params = a_function.GetParamCount();
    Call call {};
    const auto* groupFrame = std::addressof(a_frame);
    if (name == "GroupUse" && params == 1) {
        call.operation = Operation::kUse;
    } else if (name == "GroupRemove" && params == 2) {
        call.operation = Operation::kRemove;
        call.item.itemID = Argument(a_frame, 1).GetSInt();
    } else if (name == "OnSaveEquipState" && params == 4) {
        call.operation = Operation::kSave;
    } else if (name == "ProcessItem" && params == 5) {
        const auto* form = Argument(a_frame, 0).Unpack<RE::TESForm*>();
        groupFrame = a_frame.previousFrame;
        if (!form || Argument(a_frame, 1).GetSInt() != kArmorType || !IsGroupUse(groupFrame, a_frame)) {
            return std::nullopt;
        }
        call.operation = Operation::kItem;
        call.item = {.formID = form->GetFormID(), .itemID = Argument(a_frame, 4).GetSInt()};
    } else {
        return std::nullopt;
    }
    const auto group = call.operation == Operation::kSave ? static_cast<std::int32_t>(Argument(a_frame, 2).GetFloat())
                                                          : Argument(*groupFrame, 0).GetSInt();
    const auto manager = a_frame.self.GetObject();
    if (group < 0 || std::cmp_greater_equal(group, Groups::kGroupCount) || !manager) {
        return std::nullopt;
    }
    call.group = static_cast<std::size_t>(group);
    if (!ReadGroup(*manager, call)) {
        return std::nullopt;
    }
    if (call.operation == Operation::kSave) {
        auto rows = FavoritesMenu::TakeCapture(call.group);
        if (!rows) {
            return std::nullopt;
        }
        call.rows = std::move(*rows);
    }
    return call;
}

Outcome Run(const Call& a_call) {
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return {};
    }
    switch (a_call.operation) {
        case Operation::kUse:
            Equipment::FavoriteGroupActions::Use(*player, a_call.group, a_call.members, a_call.unequipArmor);
            return {};
        case Operation::kItem: {
            const auto result = Equipment::FavoriteGroupActions::EquipMember(
                *player,
                a_call.group,
                a_call.members,
                a_call.item
            );
            return {.handled = result.placed, .outfitMask = result.nativeSlotMask};
        }
        case Operation::kSave:   Groups::Capture(a_call.group, a_call.members, a_call.rows); return {};
        case Operation::kRemove: Groups::Remove(a_call.group, a_call.item.itemID); return {};
    }
    return {};
}

void Complete(RE::BSScript::Stack& a_stack, const Outcome& a_outcome) {
    auto* mask = a_stack.top->self.GetObject()->GetVariable(kOutfitMask);
    mask->SetSInt(static_cast<std::int32_t>(static_cast<std::uint32_t>(mask->GetSInt()) | a_outcome.outfitMask));
    a_stack.returnValue.SetBool(true);
}
}
