#include "Hooks.h"
#include "RenderManager.h"

namespace Hooks {


    struct RenderEndHook {
        static void thunk(uint32_t a1) {
            originalFunction(a1);
            RenderManager::Render();
        }
        static inline REL::Relocation<decltype(thunk)> originalFunction;
        static void Install() {
            auto& trampoline = SKSE::GetTrampoline();
            originalFunction = trampoline.write_call<5>(REL::RelocationID(75461, 77246).address() + REL::Relocate(0x9, 0x9), thunk);
        }
    };

}
struct CreateD3DAndSwapChain {
    static void thunk() {
        originalFunction();
        const auto renderer = RE::BSGraphics::Renderer::GetSingleton();
        if (!renderer) {
            return;
        }

        auto data = renderer->GetRuntimeData();

        auto swapChain = reinterpret_cast<IDXGISwapChain*>(data.renderWindows[0].swapChain);
        if (!swapChain) {
            logger::error("couldn't find swapChain");
            return;
        }

        DXGI_SWAP_CHAIN_DESC desc{};
        if (FAILED(swapChain->GetDesc(std::addressof(desc)))) {
            logger::error("IDXGISwapChain::GetDesc failed.");
            return;
        }

        auto device = reinterpret_cast<ID3D11Device*>(data.forwarder);
        auto context = reinterpret_cast<ID3D11DeviceContext*>(data.context);

        RenderManager::Init(device, context);
    }
    static inline REL::Relocation<decltype(thunk)> originalFunction;
    static void Install() {
        auto& trampoline = SKSE::GetTrampoline();
        const REL::Relocation<std::uintptr_t> target{REL::RelocationID(75595, 77226)};  // BSGraphics::InitD3D
        originalFunction = trampoline.write_call<5>(target.address() + REL::Relocate(0x9, 0x275), CreateD3DAndSwapChain::thunk);
    }
};

void Hooks::Install() {
    SKSE::AllocTrampoline(14*2);
    CreateD3DAndSwapChain::Install();
    RenderEndHook::Install();
}
