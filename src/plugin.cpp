#include "Plugin.h"
#include "Hooks.h"
#include "MeshRenderingFrameworkAPI.h"

void OnMessage(SKSE::MessagingInterface::Message* message) {

    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        UI::Register();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {
    SKSE::Init(skse);
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    SetupLog();
    Hooks::Install();
    logger::info("Plugin loaded");
    return true;
}
