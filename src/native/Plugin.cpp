#include "DevBenchIntegration.h"

#include "EventListener.h"
#include "Hooks.h"
#include "Localization.h"
#include "Papyrus.h"
#include "Serialization.h"
#include "Settings.h"
#include "Visuals/Attachments.h"

#include <RE/Skyrim.h> // IWYU pragma: keep
#include <SKSE/SKSE.h> // IWYU pragma: keep

#include <spdlog/common.h>

namespace {
constexpr auto kTrampolineSize = 1024;

void MessageHandler(SKSE::MessagingInterface::Message* a_message) { // NOLINT(misc-const-correctness)
    switch (a_message->type) {
        case SKSE::MessagingInterface::kPostPostLoad: DevBenchIntegration::Register(); break;
        case SKSE::MessagingInterface::kDataLoaded:
            Localization::Load("MyPreciouses");
            Settings::GetSingleton()->Load();
            Visuals::Attachments::EnableFirstPersonRingSlotForRaces();
            Hooks::Install();
            EventListener::Register();
            break;
        default: break;
    }
}

}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_extender) {
    auto initInfo = SKSE::InitInfo {
        .logPattern = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [%t] [%s:%#] %v",
        .trampoline = true,
        .trampolineSize = kTrampolineSize,
    };
    if (IsDebuggerPresent() != 0 || Settings::ReadDebugLoggingEnabled()) {
        initInfo.logLevel = spdlog::level::debug;
    }
    SKSE::Init(a_extender, initInfo);

    Serialization::Install();
    Papyrus::Register();

    SKSE::GetMessagingInterface()->RegisterListener(MessageHandler);
    return true;
}
