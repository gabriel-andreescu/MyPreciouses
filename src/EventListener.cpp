#include "EventListener.h"

#include "FingerSelectMenu.h"
#include "Inventory.h"
#include "Selection.h"
#include "UI.h"
#include "VanillaCompatibility.h"

namespace {
bool g_cancelInputSuppressed = false;
RE::INPUT_DEVICE g_cancelInputDevice {};
std::uint32_t g_cancelInputCode = 0;
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

    eventSource->AddEventSink<Events::ContainerChanged>(listener);
    eventSource->AddEventSink<Events::Equip>(listener);
    eventSource->AddEventSink<Events::SpellCast>(listener);
    eventSource->AddEventSink<Events::SwitchRaceComplete>(listener);
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

    if (!Inventory::AsRing(RE::TESForm::LookupByID(a_event->baseObject))) {
        return Control::kContinue;
    }

    Selection::QueueCheck();
    UI::QueueRefreshAfterRingEquip();
    return Control::kContinue;
}

EventListener::Control EventListener::ProcessEvent(
    const Events::SpellCast* a_event,
    [[maybe_unused]] RE::BSTEventSource<Events::SpellCast>* a_eventSource
) {
    if (!a_event) {
        return Control::kContinue;
    }

    auto* eventReference = a_event->object.get();
    auto* actor = eventReference ? eventReference->As<RE::Actor>() : nullptr;
    if (actor && actor->IsPlayerRef()) {
        VanillaCompatibility::OnPlayerSpellCast(*actor, a_event->spell);
    }

    return Control::kContinue;
}

EventListener::Control EventListener::ProcessEvent(
    const Events::SwitchRaceComplete* a_event,
    [[maybe_unused]] RE::BSTEventSource<Events::SwitchRaceComplete>* a_eventSource
) {
    if (!a_event) {
        return Control::kContinue;
    }

    auto* eventReference = a_event->subject.get();
    auto* actor = eventReference ? eventReference->As<RE::Actor>() : nullptr;
    if (actor && actor->IsPlayerRef()) {
        VanillaCompatibility::OnPlayerRaceSwitchComplete(*actor);
    }

    return Control::kContinue;
}

EventListener::Control EventListener::ProcessEvent(
    const RE::MenuOpenCloseEvent* a_event,
    [[maybe_unused]] RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource
) {
    if (a_event && !a_event->opening) {
        FingerSelectMenu::OnMenuClose(a_event->menuName);
    }

    return Control::kContinue;
}

EventListener::Control EventListener::ProcessEvent(
    const InputEvents* a_event,
    [[maybe_unused]] RE::BSTEventSource<InputEvents>* a_eventSource
) {
    const auto fingerSelectOpen = FingerSelectMenu::IsOpen();
    if (!fingerSelectOpen && !g_cancelInputSuppressed) {
        return Control::kContinue;
    }

    for (auto* event = a_event ? *a_event : nullptr; event; event = event->next) {
        const auto* button = event->AsButtonEvent();
        if (!button) {
            continue;
        }

        const auto device = button->GetDevice();
        const auto key = button->GetIDCode();

        if (g_cancelInputSuppressed && device == g_cancelInputDevice && key == g_cancelInputCode) {
            if (button->Value() <= 0.0F) {
                g_cancelInputSuppressed = false;
                return Control::kStop;
            }

            if (!button->IsDown()) {
                return Control::kStop;
            }

            g_cancelInputSuppressed = false;
        }

        if (!fingerSelectOpen || !button->IsDown()) {
            continue;
        }

        auto isCancel = false;
        if (const auto* userEvents = RE::UserEvents::GetSingleton(); userEvents) {
            isCancel = button->GetUserEvent() == userEvents->cancel;
        }

        if (!isCancel) {
            switch (button->GetDevice()) {
                case RE::INPUT_DEVICE::kKeyboard:
                    isCancel = key == RE::BSKeyboardDevice::Keys::kEscape || key == RE::BSKeyboardDevice::Keys::kTab;
                    break;
                case RE::INPUT_DEVICE::kGamepad:
                    if (const auto* controlMap = RE::ControlMap::GetSingleton();
                        controlMap && controlMap->GetGamePadType() == RE::PC_GAMEPAD_TYPE::kOrbis) {
                        isCancel = key == RE::BSPCOrbisGamepadDevice::Keys::kPS3_B;
                    } else {
                        isCancel = key == RE::BSWin32GamepadDevice::Keys::kB;
                    }
                    break;
                default: break;
            }
        }

        if (isCancel) {
            g_cancelInputSuppressed = true;
            g_cancelInputDevice = device;
            g_cancelInputCode = key;
            FingerSelectMenu::Cancel();
            return Control::kStop;
        }
    }

    return Control::kContinue;
}
