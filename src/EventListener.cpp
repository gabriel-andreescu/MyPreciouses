#include "EventListener.h"

#include "Inventory.h"
#include "Selection.h"
#include "UI.h"

void EventListener::Register() {
    auto* listener = GetSingleton();
    auto* eventSource = RE::ScriptEventSourceHolder::GetSingleton();
    if (!eventSource) {
        logger::warn("Selection: event sink unavailable | reason=noScriptEventSourceHolder");
        return;
    }

    eventSource->AddEventSink<Events::ContainerChanged>(listener);
    eventSource->AddEventSink<Events::Equip>(listener);
    logger::info("Selection: event sinks installed");
}

EventListener::Control EventListener::ProcessEvent(
    const Events::ContainerChanged* a_event,
    [[maybe_unused]] RE::BSTEventSource<Events::ContainerChanged>* a_eventSource
) {
    if (a_event) {
        Selection::OnContainerChanged(*a_event);
    }
    return Control::kContinue;
}

EventListener::Control EventListener::ProcessEvent(
    const Events::Equip* a_event,
    [[maybe_unused]] RE::BSTEventSource<Events::Equip>* a_eventSource
) {
    if (!a_event) {
        return Control::kContinue;
    }

    auto* eventReference = a_event->actor.get();
    auto* actor = eventReference ? eventReference->As<RE::Actor>() : nullptr;
    if (!actor || !actor->IsPlayerRef()) {
        return Control::kContinue;
    }

    auto* ring = Inventory::AsRing(RE::TESForm::LookupByID(a_event->baseObject));
    if (!ring) {
        return Control::kContinue;
    }

    if (!a_event->equipped) {
        UI::RefreshEquipmentSoon(ring->GetFormID());
    }
    return Control::kContinue;
}
