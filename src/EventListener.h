#pragma once

#include <REX/REX/Singleton.h>

class EventListener final :
    REX::Singleton<EventListener>,
    public RE::BSTEventSink<RE::TESContainerChangedEvent>,
    public RE::BSTEventSink<RE::TESEquipEvent>,
    public RE::BSTEventSink<RE::TESSpellCastEvent>,
    public RE::BSTEventSink<RE::TESSwitchRaceCompleteEvent>,
    public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
    public RE::BSInputDeviceManager::Sink {
public:
    static void Register();

protected:
    using Control = RE::BSEventNotifyControl;
    using InputEvents = RE::InputEvent*;

    Control ProcessEvent(
        const RE::TESContainerChangedEvent* a_event,
        RE::BSTEventSource<RE::TESContainerChangedEvent>* a_eventSource
    ) override;
    Control ProcessEvent(
        const RE::TESEquipEvent* a_event,
        RE::BSTEventSource<RE::TESEquipEvent>* a_eventSource
    ) override;
    Control ProcessEvent(
        const RE::TESSpellCastEvent* a_event,
        RE::BSTEventSource<RE::TESSpellCastEvent>* a_eventSource
    ) override;
    Control ProcessEvent(
        const RE::TESSwitchRaceCompleteEvent* a_event,
        RE::BSTEventSource<RE::TESSwitchRaceCompleteEvent>* a_eventSource
    ) override;
    Control ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource
    ) override;
    Control ProcessEvent(const InputEvents* a_event, RE::BSTEventSource<InputEvents>* a_eventSource) override;
};
