#include "ui/Overlay.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>

namespace Overlay
{
    void ConfigureLogging();
    void OnMessage(SFSE::MessagingInterface::Message* message);
}

void Overlay::ConfigureLogging()
{
    const auto directory = SFSE::GetLogDirectory();
    if (!directory) {
        return;
    }

    try {
        const auto filename = *directory / (OVERLAY_NAME ".log");
        const std::array<spdlog::sink_ptr, 2> sinks{
            std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename.string(), true),
            std::make_shared<spdlog::sinks::msvc_sink_mt>()
        };
        auto log = std::make_shared<spdlog::logger>(OVERLAY_NAME, sinks.begin(), sinks.end());
#ifdef NDEBUG
        constexpr auto level = spdlog::level::info;
#else
        constexpr auto level = spdlog::level::debug;
#endif
        log->set_level(level);
        log->flush_on(level);
        spdlog::set_default_logger(std::move(log));
        logger::set_pattern("[%T.%e] [%=5t] [%L] %v");
    } catch (const spdlog::spdlog_ex& error) {
        logger::warn("Could not open overlay log: {}", error.what());
    }
}

void Overlay::OnMessage(SFSE::MessagingInterface::Message* message)
{
    if (message && message->type == SFSE::MessagingInterface::kPostLoad) {
        if (!UI::Register()) {
            logger::warn("SFSE Menu Framework unavailable; overlay remains inactive.");
        }
    }
}

extern "C"
{
    __declspec(dllexport) constinit SFSE::PluginVersionData SFSEPlugin_Version{
        .dataVersion = 1,
        .pluginVersion = OVERLAY_PACKED_VERSION,
        .name = OVERLAY_NAME,
        .author = OVERLAY_AUTHOR,
        .structureIndependence = 1,
        .compatibleVersions = { SFSE::RUNTIME_LATEST }
    };
}

extern "C" __declspec(dllexport) bool SFSEPlugin_Load(const SFSE::LoadInterface* sfse)
{
    if (!sfse || !SFSE::Init(*sfse)) {
        return false;
    }
    Overlay::ConfigureLogging();
    logger::info("{} v{}", OVERLAY_NAME, OVERLAY_VERSION_TEXT);
    const auto messaging = SFSE::GetMessagingInterface();
    return messaging && messaging->RegisterListener(Overlay::OnMessage);
}
