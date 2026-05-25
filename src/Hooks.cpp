#include "Hooks.h"

#include "EventBindings.h"
#include "Forms.h"
#include "Inventory.h"
#include "RingEnchantments.h"
#include "RingVisuals.h"
#include "Selection.h"
#include "UI.h"
#include "VirtualRings.h"

#include <cstring>
#include <optional>

namespace Hooks {
namespace {
    enum class HandEquipSlot {
        kOther,
        kRight,
        kLeft,
    };

    [[nodiscard]] HandEquipSlot GetHandEquipSlot(const RE::BGSEquipSlot* a_slot) {
        if (!a_slot) {
            return HandEquipSlot::kOther;
        }

        switch (a_slot->GetFormID()) {
            case Forms::kRightHandEquipSlotFormID: return HandEquipSlot::kRight;
            case Forms::kLeftHandEquipSlotFormID:  return HandEquipSlot::kLeft;
            default:                               return HandEquipSlot::kOther;
        }
    }

    struct ActiveEffectSetEffectivenessHook {
        static void thunk(RE::ActiveEffect* a_effect, float a_power, bool a_onlyHostile) {
            func(a_effect, a_power, a_onlyHostile);

            auto* targetRef = a_effect && a_effect->target ? a_effect->target->GetTargetStatsObject() : nullptr;
            auto* actor = targetRef ? targetRef->As<RE::Actor>() : nullptr;
            if (!actor || !actor->IsPlayerRef()) {
                return;
            }

            auto* sourceArmor = a_effect && a_effect->source ? a_effect->source->As<RE::TESObjectARMO>() : nullptr;
            const auto scale = RingEnchantments::GetScale(*actor, sourceArmor);
            if (scale >= 1.0F) {
                return;
            }

            a_effect->magnitude *= scale;
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    void InstallEnchantmentStrengthHook() {
        stl::write_thunk_call<ActiveEffectSetEffectivenessHook>(
            REL::Relocation {RELOCATION_ID(33763, 34547), REL::Relocate(0x4A3, 0x656, 0x427)}
        );
        logger::info("Hooks: enchantment strength hook installed");
    }

    struct GetEquippedConditionHook {
        static bool thunk(RE::TESObjectREFR* a_thisObj, void* a_param1, void* a_param2, double& a_result) {
            const auto result = func(a_thisObj, a_param1, a_param2, a_result);
            if (!result || a_result != 0.0 || !a_thisObj || !a_param1) {
                return result;
            }

            auto* actor = a_thisObj->As<RE::Actor>();
            auto* getEquippedArgument = static_cast<RE::TESForm*>(a_param1);
            if (actor && VirtualRings::MatchesGetEquippedCondition(*actor, *getEquippedArgument)) {
                a_result = 1.0;
            }

            return result;
        }

        static inline RE::SCRIPT_FUNCTION::Condition_t* func {nullptr};
    };

    void InstallGetEquippedConditionHook() {
        auto* command = RE::SCRIPT_FUNCTION::LocateScriptCommand("GetEquipped"sv);
        if (!command || !command->conditionFunction) {
            logger::warn("Hooks: GetEquipped condition hook skipped | reason=commandUnavailable");
            return;
        }

        GetEquippedConditionHook::func = command->conditionFunction;
        command->conditionFunction = GetEquippedConditionHook::thunk;
        logger::info("Hooks: GetEquipped condition hook installed");
    }

    struct EquipObjectHook {
        static void thunk(
            RE::ActorEquipManager* a_equipManager,
            RE::Actor* a_actor,
            RE::TESBoundObject* a_object,
            const RE::ObjectEquipParams& a_params
        ) {
            if (!a_actor || !a_actor->IsPlayerRef()) {
                func(a_equipManager, a_actor, a_object, a_params);
                return;
            }

            auto* ring = Inventory::AsRing(a_object);
            if (ring && Selection::InterceptRightEquip(*a_actor, *ring, a_params)) {
                return;
            }

            func(a_equipManager, a_actor, a_object, a_params);
            Selection::QueueCheck();
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    void InstallEquipObserverHook() {
        stl::write_thunk_call<EquipObjectHook>(
            REL::Relocation {RELOCATION_ID(37938, 38894), REL::Relocate(0xE5, 0x170)}
        );
        logger::info("VirtualRings: equip observer installed");
    }

    [[nodiscard]] bool IsMirroredEventName(const RE::BSFixedString& a_eventName) {
        return std::strcmp(a_eventName.c_str(), "OnEquipped")
               == 0
               || std::strcmp(a_eventName.c_str(), "OnUnequipped")
               == 0;
    }

    [[nodiscard]] RE::FormID ResolveHandleFormID(const RE::VMHandle a_handle, const RE::FormType a_formType) {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        auto* handlePolicy = vm ? vm->GetObjectHandlePolicy() : nullptr;
        if (!handlePolicy || a_handle == 0) {
            return 0;
        }

        const auto* form = handlePolicy->GetObjectForHandle(a_formType, a_handle);
        return form ? form->GetFormID() : 0;
    }

    [[nodiscard]] std::optional<RE::FormID> ExtractMirroredEventActorFormID(
        const RE::BSFixedString& a_eventName,
        RE::BSScript::IFunctionArguments* a_args
    ) {
        if (!IsMirroredEventName(a_eventName) || !a_args) {
            return std::nullopt;
        }

        RE::BSScrapArray<RE::BSScript::Variable> args;
        if (!(*a_args)(args) || args.empty() || !args.front().IsObject()) {
            return std::nullopt;
        }

        const auto object = args.front().GetObject();
        const auto objectHandle = object ? object->GetHandle() : 0;
        const auto actorFormID = ResolveHandleFormID(objectHandle, RE::FormType::ActorCharacter);
        return actorFormID != 0 ? std::make_optional(actorFormID) : std::nullopt;
    }

    struct SendEventHook {
        static void thunk(
            RE::BSScript::IVirtualMachine* a_vm,
            RE::VMHandle a_handle,
            const RE::BSFixedString& a_eventName,
            RE::BSScript::IFunctionArguments* a_args
        ) {
            EventBindings::MirrorActiveTarget(
                a_handle,
                a_eventName,
                ExtractMirroredEventActorFormID(a_eventName, a_args)
            );
            func(a_vm, a_handle, a_eventName, a_args);
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    void InstallPapyrusEventMirrorHook() {
#ifndef __clang_analyzer__
        REL::Relocation<std::uintptr_t> vmVTable {RE::VTABLE_BSScript__Internal__VirtualMachine[0]};
        SendEventHook::func = vmVTable.write_vfunc(REL::Relocate(0x24, 0x24, 0x26), SendEventHook::thunk);
#endif

        logger::info("Papyrus: event hook installed | event=SendEvent");
    }

    [[nodiscard]] RE::ItemList* GetItemListFromItemSelectContext(void* a_menuContext) {
        if (!a_menuContext) {
            return nullptr;
        }

        return REL::RelocateMember<RE::ItemList*>(a_menuContext, 0x48, 0x70);
    }

    [[nodiscard]] RE::InventoryEntryData* GetSelectedEntryFromItemSelectContext(void* a_menuContext) {
        auto* itemList = GetItemListFromItemSelectContext(a_menuContext);
        auto* selectedItem = itemList ? itemList->GetSelectedItem() : nullptr;
        return selectedItem ? selectedItem->data.objDesc : nullptr;
    }

    struct InventoryItemSelectHook {
        static void thunk(void* a_menuContext, RE::BGSEquipSlot* a_slot) {
            const auto handSlot = GetHandEquipSlot(a_slot);
            if (handSlot == HandEquipSlot::kLeft) {
                auto* entry = GetSelectedEntryFromItemSelectContext(a_menuContext);
                if (UI::SelectEntryForLeftHand(entry, UI::SelectionOrigin::kInventoryMenu)) {
                    return;
                }
            }

            func(a_menuContext, a_slot);
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    void InstallInventoryItemSelectHook() {
        stl::write_thunk_branch<InventoryItemSelectHook>(
            REL::Relocation {REL::VariantID(50977, 51856, 0x8BB9C0), 0x47}
        );
        stl::write_thunk_branch<InventoryItemSelectHook>(
            REL::Relocation {REL::VariantID(50977, 51856, 0x8BB9C0), 0x66}
        );
        stl::write_thunk_branch<InventoryItemSelectHook>(
            REL::Relocation {REL::VariantID(50977, 51856, 0x8BB9C0), 0x75}
        );
        logger::info("UI: InventoryMenu ItemSelect hook installed");
    }

    struct FavoritesUseQuickslotItemHook {
        static void thunk(
            RE::ActorEquipManager* a_equipManager,
            RE::Actor* a_actor,
            RE::InventoryEntryData* a_entry,
            RE::BGSEquipSlot* a_slot,
            bool a_queueEquip
        ) {
            const auto handSlot = GetHandEquipSlot(a_slot);
            if (a_actor
                && a_actor->IsPlayerRef()
                && handSlot
                == HandEquipSlot::kLeft
                && UI::SelectEntryForLeftHand(a_entry, UI::SelectionOrigin::kFavoritesMenu)) {
                return;
            }

            func(a_equipManager, a_actor, a_entry, a_slot, a_queueEquip);
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    void InstallUI() {
        UI::InstallMenuEventSink();
        InstallInventoryItemSelectHook();
        UI::RegisterInventoryData();

        stl::write_thunk_call<FavoritesUseQuickslotItemHook>(
            REL::Relocation {REL::VariantID(50654, 51548, 0x8A5110), REL::Relocate(0xC4, 0xC2)}
        );
        logger::info("UI: FavoritesMenu quickslot hook installed");
    }

    struct CharacterLoad3DHook {
        static RE::NiAVObject* thunk(RE::Character* a_actor, bool a_backgroundLoading) {
            auto* result = func(a_actor, a_backgroundLoading);
            if (result && a_actor && a_actor->IsPlayerRef()) {
                RingVisuals::RequestRefresh();
            }
            return result;
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    void InstallLoad3DHook() {
#ifndef __clang_analyzer__
        REL::Relocation<std::uintptr_t> vTable {RE::Character::VTABLE[0]};
        CharacterLoad3DHook::func = vTable.write_vfunc(0x6A, CharacterLoad3DHook::thunk);
#endif
        logger::info("RingVisuals: Character Load3D hook installed");
    }
}

void Install() {
    InstallUI();
    InstallEquipObserverHook();
    InstallGetEquippedConditionHook();
    InstallEnchantmentStrengthHook();
    InstallPapyrusEventMirrorHook();
    InstallLoad3DHook();
}
}
