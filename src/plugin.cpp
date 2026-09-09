#include "ui/Overlay.h"

namespace
{
    void OnMessage(SFSE::MessagingInterface::Message* message)
    {
        if (message && message->type == SFSE::MessagingInterface::kPostLoad) {
            if (!Overlay::UI::Register()) {
                logger::warn("SFSE Menu Framework unavailable; overlay remains inactive.");
            }
        }
    }
}

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* sfse)
{
    if (!sfse) {
        return false;
    }
    constexpr SFSE::InitInfo init{ .trampoline = false, .hook = false };
    SFSE::Init(sfse, init);
    const auto* messaging = SFSE::GetMessagingInterface();
    return messaging && messaging->RegisterListener(OnMessage);
}
