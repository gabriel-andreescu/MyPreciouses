#include "Hooks.h"

#include "EventBindings.h"
#include "Inventory.h"
#include "MeshRetargeting.h"
#include "RuntimeEquipment.h"
#include "Selection.h"
#include "Settings.h"
#include "Slots.h"
#include "UI.h"

#include <RE/S/SendUIMessage.h>

#include <cstdint>
#include <cstring>
#include <optional>

namespace Hooks {
namespace {
    constexpr RE::FormID kRightHandEquipSlotFormID = 0x00013F42;
    constexpr RE::FormID kLeftHandEquipSlotFormID = 0x00013F43;

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
            case kRightHandEquipSlotFormID: return HandEquipSlot::kRight;
            case kLeftHandEquipSlotFormID:  return HandEquipSlot::kLeft;
            default:                        return HandEquipSlot::kOther;
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
            const auto scale = RuntimeEquipment::GetEnchantmentScale(*actor, sourceArmor);
            if (scale >= 1.0F) {
                return;
            }

            a_effect->magnitude *= scale;
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    void InstallEnchantmentPowerHook() {
        stl::write_thunk_call<ActiveEffectSetEffectivenessHook>(
            REL::Relocation {RELOCATION_ID(33763, 34547), REL::Relocate(0x4A3, 0x656, 0x427)}
        );
        logger::info("Hooks: enchantment power hook installed");
    }

    struct SlotReplacement {
        RE::TESObjectARMO* armor {nullptr};
        DisplaySlot channel {DisplaySlot::kRegular};
    };

    [[nodiscard]] std::optional<SlotReplacement> AsLeftSlotArmor(RE::TESBoundObject* a_object) {
        auto* armor = a_object ? a_object->As<RE::TESObjectARMO>() : nullptr;
        if (!armor || RuntimeEquipment::IsArmor(armor)) {
            return std::nullopt;
        }

        if (armor->HasPartOf(Slots::GetArmorSlot(DisplaySlot::kRegular))) {
            return SlotReplacement {
                .armor = armor,
                .channel = DisplaySlot::kRegular,
            };
        }

        if (Settings::GetSingleton()->IsBondOfMatrimonyEnabled()
            && armor->HasPartOf(Slots::GetArmorSlot(DisplaySlot::kBond))) {
            return SlotReplacement {
                .armor = armor,
                .channel = DisplaySlot::kBond,
            };
        }

        return std::nullopt;
    }

    void ClearVirtualLeftSlotForReplacement(
        RE::Actor& a_actor,
        const RE::TESObjectARMO& a_replacement,
        const DisplaySlot a_channel
    ) {
        if (a_actor.GetWornArmor(Slots::GetArmorSlot(a_channel)) != std::addressof(a_replacement)) {
            return;
        }

        Selection::Clear(a_channel);
        RuntimeEquipment::Clear(a_channel);
        RE::SendUIMessage::SendInventoryUpdateMessage(std::addressof(a_actor), std::addressof(a_replacement));
        UI::RefreshRows();
    }

    void SyncAfterEquip(RE::Actor& a_actor) {
        auto refreshed = false;
        for (const auto channel : kDisplaySlots) {
            const auto sourceFormID = RuntimeEquipment::SyncAfterEquip(a_actor, channel);
            if (!sourceFormID) {
                continue;
            }

            auto* sourceRing = RE::TESForm::LookupByID<RE::TESObjectARMO>(*sourceFormID);
            RE::SendUIMessage::SendInventoryUpdateMessage(std::addressof(a_actor), sourceRing);
            refreshed = true;
        }

        if (refreshed) {
            UI::RefreshRows();
        }
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

            auto leftSlotReplacement = AsLeftSlotArmor(a_object);
            auto* armor = a_object ? a_object->As<RE::TESObjectARMO>() : nullptr;
            const auto isRuntimeArmor = RuntimeEquipment::IsArmor(armor);
            auto* ring = Inventory::AsRing(a_object);
            if (!ring) {
                func(a_equipManager, a_actor, a_object, a_params);
                if (leftSlotReplacement) {
                    ClearVirtualLeftSlotForReplacement(
                        *a_actor,
                        *leftSlotReplacement->armor,
                        leftSlotReplacement->channel
                    );
                }
                if (!isRuntimeArmor) {
                    SyncAfterEquip(*a_actor);
                }
                return;
            }

            if (Selection::InterceptRightEquip(*a_actor, *ring, a_params)) {
                return;
            }

            func(a_equipManager, a_actor, a_object, a_params);
            if (leftSlotReplacement) {
                ClearVirtualLeftSlotForReplacement(*a_actor, *leftSlotReplacement->armor, leftSlotReplacement->channel);
            }

            SyncAfterEquip(*a_actor);
            UI::RefreshEquipmentSoon(ring->GetFormID());
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    void InstallEquipObserverHook() {
        stl::write_thunk_call<EquipObjectHook>(
            REL::Relocation {RELOCATION_ID(37938, 38894), REL::Relocate(0xE5, 0x170)}
        );
        logger::info("RuntimeEquipment: equip observer installed");
    }

    template <class T>
    void HandleAddAddonNodes(
        RE::NiAVObject* a_clonedNode,
        RE::NiAVObject* a_node,
        std::int32_t a_slot,
        RE::TESObjectREFR* a_actor,
        RE::BSTSmartPointer<RE::BipedAnim>& a_biped
    ) {
        auto context = MeshRetargeting::CaptureAttachContext(a_slot, a_actor, a_biped);
        T::func(a_clonedNode, a_node, a_slot, a_actor, a_biped);
        if (context) {
            MeshRetargeting::QueueReplacement(std::move(*context));
        }
    }

    struct AddAddonNodesCall1 {
        static void thunk(
            RE::NiAVObject* a_clonedNode,
            RE::NiAVObject* a_node,
            std::int32_t a_slot,
            RE::TESObjectREFR* a_actor,
            RE::BSTSmartPointer<RE::BipedAnim>& a_biped
        ) {
            HandleAddAddonNodes<AddAddonNodesCall1>(a_clonedNode, a_node, a_slot, a_actor, a_biped);
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    struct AddAddonNodesCall2 {
        static void thunk(
            RE::NiAVObject* a_clonedNode,
            RE::NiAVObject* a_node,
            std::int32_t a_slot,
            RE::TESObjectREFR* a_actor,
            RE::BSTSmartPointer<RE::BipedAnim>& a_biped
        ) {
            HandleAddAddonNodes<AddAddonNodesCall2>(a_clonedNode, a_node, a_slot, a_actor, a_biped);
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    struct AddAddonNodesCall3 {
        static void thunk(
            RE::NiAVObject* a_clonedNode,
            RE::NiAVObject* a_node,
            std::int32_t a_slot,
            RE::TESObjectREFR* a_actor,
            RE::BSTSmartPointer<RE::BipedAnim>& a_biped
        ) {
            HandleAddAddonNodes<AddAddonNodesCall3>(a_clonedNode, a_node, a_slot, a_actor, a_biped);
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    void InstallMeshRetargetingHooks() {
        constexpr REL::RelocationID attachUpdate {15501, 15678};
        stl::write_thunk_call<AddAddonNodesCall1>(REL::Relocation {attachUpdate, REL::Relocate(0xCBF, 0xE11, 0xDF0)});
        stl::write_thunk_call<AddAddonNodesCall2>(
            REL::Relocation {attachUpdate, REL::Relocate(0x3023, 0x32D3, 0x32D3)}
        );
        stl::write_thunk_call<AddAddonNodesCall3>(
            REL::Relocation {attachUpdate, REL::Relocate(0x36B7, 0x3967, 0x3967)}
        );
        logger::info("MeshRetargeting: AddAddonNodes hooks installed");
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
            if (GetHandEquipSlot(a_slot) == HandEquipSlot::kLeft) {
                auto* entry = GetSelectedEntryFromItemSelectContext(a_menuContext);
                if (UI::SelectEntryForLeftHand(entry)) {
                    return;
                }
            }

            func(a_menuContext, a_slot);
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    void InstallInventoryItemSelectHook() {
        // SE/AE/VR: InventoryMenu::ItemSelect + 0x47, +0x66, +0x75
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
            if (a_actor
                && a_actor->IsPlayerRef()
                && GetHandEquipSlot(a_slot)
                == HandEquipSlot::kLeft
                && UI::SelectEntryForLeftHand(a_entry)) {
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

        // SE/VR: FavoritesMenu::UseQuickslotItem + 0xC4
        // AE:    FavoritesMenu::UseQuickslotItem + 0xC2
        stl::write_thunk_call<FavoritesUseQuickslotItemHook>(
            REL::Relocation {REL::VariantID(50654, 51548, 0x8A5110), REL::Relocate(0xC4, 0xC2)}
        );
        logger::info("UI: FavoritesMenu quickslot hook installed");
    }
}

void Install() {
    InstallUI();
    InstallEquipObserverHook();
    InstallEnchantmentPowerHook();
    InstallPapyrusEventMirrorHook();
    InstallMeshRetargetingHooks();
}
}
