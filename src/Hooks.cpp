#include "Hooks.h"

#include "Equipment/AssignmentActions.h"
#include "Inventory.h"
#include "UI.h"
#include "UI/ItemMenuActions.h"
#include "VirtualSlots.h"

#include <optional>
#include <string_view>

namespace Hooks {
namespace {
    constexpr RE::FormID kRightHandEquipSlotFormID {0x00013F42};
    constexpr RE::FormID kLeftHandEquipSlotFormID {0x00013F43};

    [[nodiscard]] std::optional<Core::Hand> GetEquipHand(const RE::BGSEquipSlot* a_slot) {
        if (!a_slot) {
            return std::nullopt;
        }

        switch (a_slot->GetFormID()) {
            case kRightHandEquipSlotFormID: return Core::Hand::kRight;
            case kLeftHandEquipSlotFormID:  return Core::Hand::kLeft;
            default:                        return std::nullopt;
        }
    }

    void RefreshRingItemRowsAfterReconciliation(const Equipment::ActionResult a_result) {
        if (a_result.selectionChanged) {
            UI::RefreshRingItemRows();
        }
    }

    void QueueInventoryRefreshAfterEquipmentAction(
        const Core::ActorKey a_actor,
        const RE::FormID a_sourceFormID,
        const Equipment::ActionResult a_result
    ) {
        stl::add_ui_task([a_actor, a_sourceFormID, a_result] {
            UI::RefreshItemRowsAfterEquipmentAction(UI::ItemMenuHost::kInventory, a_actor, a_sourceFormID, a_result);
        });
    }

    struct ActiveEffectSetEffectivenessHook {
        static void thunk(RE::ActiveEffect* a_effect, float a_power, bool a_onlyHostile) {
            func(a_effect, a_power, a_onlyHostile);

            auto* targetRef = a_effect && a_effect->target ? a_effect->target->GetTargetStatsObject() : nullptr;
            auto* actor = targetRef ? targetRef->As<RE::Actor>() : nullptr;
            if (!actor) {
                return;
            }

            auto* sourceArmor = a_effect && a_effect->source ? a_effect->source->As<RE::TESObjectARMO>() : nullptr;
            const auto scale = VirtualSlots::GetRingEnchantmentScaleForSource(*actor, sourceArmor);
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
            if (actor && VirtualSlots::MatchesGetEquippedCondition(*actor, *getEquippedArgument)) {
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
            const auto actorKey = Core::MakeActorKey(*a_actor);
            if (ring
                && Equipment::InterceptRightEquip(
                    *a_actor,
                    *ring,
                    a_params,
                    [actorKey, sourceFormID = ring->GetFormID()](const auto result) {
                        QueueInventoryRefreshAfterEquipmentAction(actorKey, sourceFormID, result);
                    }
                )) {
                return;
            }

            func(a_equipManager, a_actor, a_object, a_params);
            Equipment::QueueAssignmentReconciliation(actorKey, RefreshRingItemRowsAfterReconciliation);
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    void InstallEquipObjectHook() {
        stl::write_thunk_call<EquipObjectHook>(
            REL::Relocation {RELOCATION_ID(37938, 38894), REL::Relocate(0xE5, 0x170)}
        );
        logger::info("Hooks: EquipObject hook installed");
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
            if (const auto hand = GetEquipHand(a_slot)) {
                auto* entry = GetSelectedEntryFromItemSelectContext(a_menuContext);
                if (UI::ItemMenuActions::HandleRingUseFromMenuEntry(
                        entry,
                        *hand,
                        UI::ItemMenuHost::kInventory,
                        Core::GetPlayerActorKey()
                    )) {
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
        logger::info("Hooks: InventoryMenu ItemSelect hook installed");
    }

    struct FavoritesUseQuickslotItemHook {
        static void thunk(
            RE::ActorEquipManager* a_equipManager,
            RE::Actor* a_actor,
            RE::InventoryEntryData* a_entry,
            RE::BGSEquipSlot* a_slot,
            bool a_queueEquip
        ) {
            const auto hand = GetEquipHand(a_slot);
            if (a_actor
                && a_actor->IsPlayerRef()
                && hand
                && UI::ItemMenuActions::HandleRingUseFromMenuEntry(
                    a_entry,
                    *hand,
                    UI::ItemMenuHost::kFavorites,
                    Core::MakeActorKey(*a_actor)
                )) {
                return;
            }

            func(a_equipManager, a_actor, a_entry, a_slot, a_queueEquip);
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    void InstallItemMenuHooks() {
        InstallInventoryItemSelectHook();
        UI::RegisterItemMenuDataCallback();

        stl::write_thunk_call<FavoritesUseQuickslotItemHook>(
            REL::Relocation {REL::VariantID(50654, 51548, 0x8A5110), REL::Relocate(0xC4, 0xC2)}
        );
        logger::info("Hooks: FavoritesMenu quickslot hook installed");
    }

    struct CharacterLoad3DHook {
        static RE::NiAVObject* thunk(RE::Character* a_actor, bool a_backgroundLoading) {
            auto* result = func(a_actor, a_backgroundLoading);
            if (result && a_actor && a_actor->IsPlayerRef()) {
                VirtualSlots::RequestVisualRefresh(Core::MakeActorKey(*a_actor));
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
        logger::info("Hooks: Character Load3D hook installed");
    }
}

void Install() {
    InstallItemMenuHooks();
    InstallEquipObjectHook();
    InstallGetEquippedConditionHook();
    InstallEnchantmentStrengthHook();
    InstallLoad3DHook();
}
}
