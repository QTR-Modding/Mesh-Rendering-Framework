#include "Plugin.h"
#include "MeshRenderingFrameworkAPI.h"
#include "RenderManager.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace
{
    struct RenderUpdateLoop {
        static constexpr std::chrono::milliseconds tickInterval{10};

        static void QueueTick()
        {
            if (tickPending.exchange(true)) {
                return;
            }

            const SKSE::TaskInterface* taskInterface = SKSE::GetTaskInterface();
            if (!taskInterface) {
                tickPending.store(false);
                return;
            }

            taskInterface->AddTask([]() {
                RenderManager::RenderPending();
                tickPending.store(false);
            });
        }

        static void Run(std::stop_token stopToken)
        {
            while (!stopToken.stop_requested()) {
                std::this_thread::sleep_for(tickInterval);
                if (!stopToken.stop_requested()) {
                    QueueTick();
                }
            }
        }

        static void Start()
        {
            if (!worker.joinable()) {
                worker = std::jthread(Run);
            }
        }

        static inline std::atomic_bool tickPending = false;
        static inline std::jthread worker;
    };
}

void OnMessage(SKSE::MessagingInterface::Message* message) {

    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        RE::BSGraphics::Renderer* renderer = RE::BSGraphics::Renderer::GetSingleton();
        if (!renderer) {
            logger::error("Could not initialize mesh renderer: Skyrim renderer is unavailable");
            return;
        }

        RE::BSGraphics::RendererData& runtimeData = renderer->GetRuntimeData();
        ID3D11Device* device = reinterpret_cast<ID3D11Device*>(runtimeData.forwarder);
        ID3D11DeviceContext* context = reinterpret_cast<ID3D11DeviceContext*>(runtimeData.context);
        if (!RenderManager::Init(device, context)) {
            logger::error("Could not initialize synchronous mesh renderer");
            return;
        }

        RenderUpdateLoop::Start();
        UI::Register();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {
    SKSE::Init(skse);
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    SetupLog();
    logger::info("Plugin loaded");
    return true;
}
