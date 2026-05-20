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
                return;
            }

            if (Selection::InterceptRightEquip(*a_actor, *ring, a_params)) {
                return;
            }

            func(a_equipManager, a_actor, a_object, a_params);
            if (leftSlotReplacement) {
                ClearVirtualLeftSlotForReplacement(*a_actor, *leftSlotReplacement->armor, leftSlotReplacement->channel);
            }

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
        auto context = MeshRetargeting::CaptureAttachContext(a_clonedNode, a_node, a_slot, a_actor, a_biped);
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
        SendEventHook::func = vmVTable.write_vfunc(0x24, SendEventHook::thunk);
#endif

        logger::info("Papyrus: event hook installed | event=SendEvent");
    }

    struct MenuControlsRightClickHook {
        static RE::BSEventNotifyControl thunk(
            RE::MenuControls* a_menuControls,
            RE::InputEvent* const* a_event,
            RE::BSTEventSource<RE::InputEvent*>* a_eventSource
        ) {
            if (a_event && *a_event) {
                auto* ui = RE::UI::GetSingleton();
                const auto inventoryOpen = ui && ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME);

                if (inventoryOpen) {
                    for (auto* event = *a_event; event; event = event->next) {
                        if (UI::IsRightMouseDown(*event) && UI::HandleRightClick()) {
                            return RE::BSEventNotifyControl::kStop;
                        }
                    }
                }
            }

            return func(a_menuControls, a_event, a_eventSource);
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    void InstallMenuControlsHook() {
#ifndef __clang_analyzer__
        REL::Relocation<std::uintptr_t> menuControlsVTable {RE::VTABLE_MenuControls[0]};
        MenuControlsRightClickHook::func = menuControlsVTable.write_vfunc(0x1, MenuControlsRightClickHook::thunk);
#endif
        logger::info("UI: InventoryMenu right-click hook installed");
    }

    struct FavoritesUseQuickslotItemHook {
        static void thunk(
            RE::ActorEquipManager* a_equipManager,
            RE::Actor* a_actor,
            RE::InventoryEntryData* a_entry,
            RE::BGSEquipSlot* a_slot,
            bool a_queueEquip
        ) {
            if (a_actor && a_actor->IsPlayerRef() && UI::ConsumeFavoritesRightClick()) {
                if (UI::SelectForLeftHand(a_entry)) {
                    return;
                }
            }

            func(a_equipManager, a_actor, a_entry, a_slot, a_queueEquip);
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    void InstallUIHooks() {
        UI::InstallSinks();
        InstallMenuControlsHook();
        UI::RegisterInventoryData();

        stl::write_thunk_call<FavoritesUseQuickslotItemHook>(
            REL::Relocation {RELOCATION_ID(50654, 51548), REL::Relocate(0xC4, 0xC2)}
        );
        logger::info("UI: FavoritesMenu quickslot hook installed");
    }
}

void Install() {
    InstallUIHooks();
    InstallEquipObserverHook();
    InstallEnchantmentPowerHook();
    InstallPapyrusEventMirrorHook();
    InstallMeshRetargetingHooks();
}
}
