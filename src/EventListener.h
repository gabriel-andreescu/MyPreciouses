#pragma once

#include "Events.h"

#include <REX/REX/Singleton.h>

class EventListener final :
    REX::Singleton<EventListener>,
    public RE::BSTEventSink<Events::ContainerChanged>,
    public RE::BSTEventSink<Events::Equip> {
public:
    static void Register();

protected:
    using Control = RE::BSEventNotifyControl;

    Control ProcessEvent(
        const Events::ContainerChanged* a_event,
        RE::BSTEventSource<Events::ContainerChanged>* a_eventSource
    ) override;
    Control ProcessEvent(const Events::Equip* a_event, RE::BSTEventSource<Events::Equip>* a_eventSource) override;
};
