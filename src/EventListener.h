#pragma once

#include "Events.h"

#include <REX/REX/Singleton.h>

class EventListener final :
    REX::Singleton<EventListener>,
    public RE::BSTEventSink<Events::ContainerChanged>,
    public RE::BSTEventSink<Events::Equip>,
    public RE::BSTEventSink<Events::SpellCast>,
    public RE::BSTEventSink<Events::SwitchRaceComplete>,
    public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
    public RE::BSInputDeviceManager::Sink {
public:
    static void Register();

protected:
    using Control = RE::BSEventNotifyControl;
    using InputEvents = RE::InputEvent*;

    Control ProcessEvent(
        const Events::ContainerChanged* a_event,
        RE::BSTEventSource<Events::ContainerChanged>* a_eventSource
    ) override;
    Control ProcessEvent(const Events::Equip* a_event, RE::BSTEventSource<Events::Equip>* a_eventSource) override;
    Control ProcessEvent(
        const Events::SpellCast* a_event,
        RE::BSTEventSource<Events::SpellCast>* a_eventSource
    ) override;
    Control ProcessEvent(
        const Events::SwitchRaceComplete* a_event,
        RE::BSTEventSource<Events::SwitchRaceComplete>* a_eventSource
    ) override;
    Control ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource
    ) override;
    Control ProcessEvent(const InputEvents* a_event, RE::BSTEventSource<InputEvents>* a_eventSource) override;
};
