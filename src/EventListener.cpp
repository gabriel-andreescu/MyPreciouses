#include "EventListener.h"

#include "Compatibility/Vanilla.h"
#include "Equipment/AssignmentActions.h"
#include "Equipment/AutoEquip.h"
#include "Equipment/RaceSwitchRestore.h"
#include "Inventory.h"
#include "UI.h"
#include "UI/FingerSelectMenu.h"

#include <RE/C/ContainerMenu.h>

#include <utility>

namespace {
void RefreshRingItemRowsAfterReconciliation(const Equipment::ActionResult a_result) {
    if (a_result.selectionChanged) {
        UI::RefreshRingItemRows();
    }
}
}

void EventListener::Register() {
    auto* listener = GetSingleton();
    auto* eventSource = RE::ScriptEventSourceHolder::GetSingleton();

    if (!listener) {
        logger::critical("EventListener registration failed: listener singleton is null");
        return;
    }
    if (!eventSource) {
        logger::critical("EventListener registration failed: ScriptEventSourceHolder is null");
        return;
    }

    auto* ui = RE::UI::GetSingleton();
    if (!ui) {
        logger::critical("EventListener registration failed: UI is null");
        return;
    }

    eventSource->AddEventSink<RE::TESContainerChangedEvent>(listener);
    eventSource->AddEventSink<RE::TESEquipEvent>(listener);
    eventSource->AddEventSink<RE::TESSpellCastEvent>(listener);
    // Restore virtual rings before SkyrimVM forwards race-switch completion to Papyrus listeners.
    eventSource->PrependEventSink<RE::TESSwitchRaceCompleteEvent>(listener);
    ui->AddEventSink<RE::MenuOpenCloseEvent>(listener);

    auto* input = RE::BSInputDeviceManager::GetSingleton();
    if (!input) {
        logger::critical("EventListener registration failed: BSInputDeviceManager is null");
        return;
    }

    input->PrependEventSink(static_cast<RE::BSInputDeviceManager::Sink*>(listener));
    logger::info("EventListener registered");
}

EventListener::Control EventListener::ProcessEvent(
    const RE::TESContainerChangedEvent* a_event,
    [[maybe_unused]] RE::BSTEventSource<RE::TESContainerChangedEvent>* a_eventSource
) {
    if (a_event) {
        Equipment::AutoEquip::HandleContainerChanged(*a_event);
        Equipment::HandleContainerChangedForAssignments(
            Core::GetPlayerActorKey(),
            *a_event,
            RefreshRingItemRowsAfterReconciliation
        );
    }
    return Control::kContinue;
}

EventListener::Control EventListener::ProcessEvent(
    const RE::TESEquipEvent* a_event,
    [[maybe_unused]] RE::BSTEventSource<RE::TESEquipEvent>* a_eventSource
) {
    if (!a_event) {
        return Control::kContinue;
    }

    auto* eventReference = a_event->actor.get();
    auto* actor = eventReference ? eventReference->As<RE::Actor>() : nullptr;
    if (!actor) {
        return Control::kContinue;
    }

    if (!Inventory::AsRing(RE::TESForm::LookupByID(a_event->baseObject))) {
        return Control::kContinue;
    }

    if (!actor->IsPlayerRef()) {
        Equipment::AutoEquip::HandleEquipEvent(*actor, a_event->baseObject);
        return Control::kContinue;
    }

    Equipment::QueueAssignmentReconciliation(Core::MakeActorKey(*actor), RefreshRingItemRowsAfterReconciliation);
    UI::QueueFavoritesRefreshAfterRingEquip();
    return Control::kContinue;
}

EventListener::Control EventListener::ProcessEvent(
    const RE::TESSpellCastEvent* a_event,
    [[maybe_unused]] RE::BSTEventSource<RE::TESSpellCastEvent>* a_eventSource
) {
    if (!a_event) {
        return Control::kContinue;
    }

    auto* eventReference = a_event->object.get();
    auto* actor = eventReference ? eventReference->As<RE::Actor>() : nullptr;
    if (actor && actor->IsPlayerRef()) {
        Compatibility::Vanilla::HandleSpellCast(*actor, a_event->spell);
    }

    return Control::kContinue;
}

EventListener::Control EventListener::ProcessEvent(
    const RE::TESSwitchRaceCompleteEvent* a_event,
    [[maybe_unused]] RE::BSTEventSource<RE::TESSwitchRaceCompleteEvent>* a_eventSource
) {
    if (!a_event) {
        return Control::kContinue;
    }

    auto* eventReference = a_event->subject.get();
    auto* actor = eventReference ? eventReference->As<RE::Actor>() : nullptr;
    if (!actor) {
        return Control::kContinue;
    }

    const auto actorKey = Core::MakeActorKey(*actor);
    const auto restoredVirtualRings = Equipment::RaceSwitchRestore::HandleRaceSwitchComplete(*actor);
    if (restoredVirtualRings) {
        Equipment::CompletionCallback onComplete;
        if (actor->IsPlayerRef()) {
            // Vanilla should never need this since menus pause the game, but this is useful for mods
            // that unpause menus, like Skyrim Souls RE.
            UI::RefreshRingItemRows();
            onComplete = RefreshRingItemRowsAfterReconciliation;
        }
        Equipment::QueueAssignmentReconciliation(actorKey, std::move(onComplete));
    }

    if (actor->IsPlayerRef()) {
        Compatibility::Vanilla::HandleRaceSwitchComplete(*actor);
    }

    return Control::kContinue;
}

EventListener::Control EventListener::ProcessEvent(
    const RE::MenuOpenCloseEvent* a_event,
    [[maybe_unused]] RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource
) {
    if (!a_event) {
        return Control::kContinue;
    }

    UI::HandleMenuOpenCloseEvent(*a_event);
    if (a_event->menuName == RE::ContainerMenu::MENU_NAME.data()) {
        if (a_event->opening) {
            Equipment::AutoEquip::HandleContainerMenuOpened();
        } else {
            Equipment::AutoEquip::HandleContainerMenuClosed();
        }
    }
    return Control::kContinue;
}

EventListener::Control EventListener::ProcessEvent(
    const InputEvents* a_event,
    [[maybe_unused]] RE::BSTEventSource<InputEvents>* a_eventSource
) {
    return UI::FingerSelectMenu::ConsumeInput(a_event) ? Control::kStop : Control::kContinue;
}
