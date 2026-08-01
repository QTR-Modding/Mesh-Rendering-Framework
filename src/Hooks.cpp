#include "Hooks.h"

#include "RenderManager.h"

namespace Hooks
{
    namespace
    {
        struct CreateD3DAndSwapChain {
            static void thunk()
            {
                originalFunction();

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
                }
            }

            static void Install()
            {
                SKSE::Trampoline& trampoline = SKSE::GetTrampoline();
                // BSGraphics::InitD3D, after the engine has created the device and context.
                const REL::Relocation<std::uintptr_t> target{REL::RelocationID(75595, 77226)};
                originalFunction = trampoline.write_call<5>(
                    target.address() + REL::Relocate(0x9, 0x275),
                    thunk);
            }

            static inline REL::Relocation<decltype(thunk)> originalFunction;
        };
    }

    void Install()
    {
        SKSE::AllocTrampoline(14);
        CreateD3DAndSwapChain::Install();
    }
}
