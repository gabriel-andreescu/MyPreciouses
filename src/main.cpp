#include "EventListener.h"
#include "Hooks.h"
#include "Localization.h"
#include "Papyrus.h"
#include "Serialization.h"
#include "Settings.h"
#include "VirtualSlots.h"
#include "Visuals/Attachments.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>

namespace {
constexpr auto kTrampolineSize = 1024;

void QueueDelayedVirtualSlotRefresh() {
    stl::add_thread_task(
        [] {
            VirtualSlots::RequestRefresh(Core::GetPlayerActorKey());
        },
        250ms
    );
}

[[nodiscard]] spdlog::level::level_enum GetDefaultLogLevel() {
#ifdef NDEBUG
    return spdlog::level::info;
#else
    return spdlog::level::debug;
#endif
}

void MessageHandler(SKSE::MessagingInterface::Message* a_msg) {
    switch (a_msg->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            Localization::Load("LeftHandRingsSKSE");
            Settings::GetSingleton()->Load();
            Visuals::Attachments::EnableFirstPersonRingSlotForRaces();
            Hooks::Install();
            EventListener::Register();
            break;
        case SKSE::MessagingInterface::kNewGame:
        case SKSE::MessagingInterface::kPostLoadGame: QueueDelayedVirtualSlotRefresh(); break;
        default:                                      break;
    }
}

void InitializeLogging() {
    const auto debuggerAttached = IsDebuggerPresent();
    const auto debugLoggingEnabled = Settings::ReadDebugLoggingEnabled();

    std::shared_ptr<spdlog::sinks::sink> sink;
    if (debuggerAttached) {
        sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
    } else {
        auto path = SKSE::log::log_directory();
        if (!path) {
            stl::report_and_fail("Failed to find standard logging directory"sv);
        }

        const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
        *path /= std::format("{}.log", plugin->GetName());
        sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
    }

    auto logger = std::make_shared<spdlog::logger>("Global", std::move(sink));
    logger->set_level((debuggerAttached || debugLoggingEnabled) ? spdlog::level::debug : GetDefaultLogLevel());
    logger->flush_on(spdlog::level::trace);
    spdlog::set_default_logger(std::move(logger));
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [%t] [%s:%#] %v");
}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    InitializeLogging();

    SKSE::Init(a_skse, false);
    SKSE::AllocTrampoline(kTrampolineSize);

    Serialization::Install();
    Papyrus::Register();

    const auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        logger::critical("SKSE: messaging interface unavailable");
        return false;
    }

    messaging->RegisterListener(MessageHandler);
    logger::info("LeftHandRingsSKSE loaded");
    return true;
}
