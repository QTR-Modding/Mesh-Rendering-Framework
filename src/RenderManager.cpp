#include "RenderManager.h"
#include "RE/R/RendererShadowState.h"
#include <d3d11.h>
#include <DirectXTex.h>
#include <wincodec.h>

#include <array>

constexpr float cameraZ = 320;
constexpr std::uint32_t rasterizerSlotCount = 16;

template <class T>
static void ReleaseIfExists(T*& resource) {
    if (resource) {
        resource->Release();
        resource = nullptr;
    }
}

struct RasterizerStateSnapshot {
    std::array<REX::W32::D3D11_VIEWPORT, rasterizerSlotCount> viewports{};
    std::array<REX::W32::D3D11_RECT, rasterizerSlotCount> scissorRects{};
    std::uint32_t viewportCount{rasterizerSlotCount};
    std::uint32_t scissorRectCount{rasterizerSlotCount};
};

struct RendererShadowStateSnapshot {
    RE::BSGraphics::RendererShadowState* shadowState{nullptr};
    RE::BSGraphics::RendererShadowState::FLAT_RUNTIME_DATA data{};
};

static RasterizerStateSnapshot StoreRasterizerState(REX::W32::ID3D11DeviceContext* context) {
    RasterizerStateSnapshot snapshot{};
    context->RSGetViewports(&snapshot.viewportCount, snapshot.viewports.data());
    context->RSGetScissorRects(&snapshot.scissorRectCount, snapshot.scissorRects.data());
    return snapshot;
}

static void RestoreRasterizerState(REX::W32::ID3D11DeviceContext* context, const RasterizerStateSnapshot& snapshot) {
    context->RSSetViewports(snapshot.viewportCount, snapshot.viewportCount > 0 ? snapshot.viewports.data() : nullptr);
    context->RSSetScissorRects(snapshot.scissorRectCount, snapshot.scissorRectCount > 0 ? snapshot.scissorRects.data() : nullptr);
}

static void SetRenderTargetRasterizerState(REX::W32::ID3D11DeviceContext* context, const RenderTarget& target) {
    if (target.width == 0 || target.height == 0) {
        return;
    }

    const REX::W32::D3D11_VIEWPORT viewport{
        0.0f,
        0.0f,
        static_cast<float>(target.width),
        static_cast<float>(target.height),
        0.0f,
        1.0f
    };
    const REX::W32::D3D11_RECT scissorRect{
        0,
        0,
        static_cast<std::int32_t>(target.width),
        static_cast<std::int32_t>(target.height)
    };

    context->RSSetViewports(1, &viewport);
    context->RSSetScissorRects(1, &scissorRect);
}

static RendererShadowStateSnapshot StoreRendererShadowState() {
    RendererShadowStateSnapshot snapshot{};
    snapshot.shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();
    if (snapshot.shadowState) {
        snapshot.data = snapshot.shadowState->GetRuntimeData();
    }
    return snapshot;
}

static void RestoreRendererShadowState(const RendererShadowStateSnapshot& snapshot) {
    if (snapshot.shadowState) {
        snapshot.shadowState->GetRuntimeData() = snapshot.data;
    }
}

static void SetRenderTargetShadowState(const RenderTarget& target) {
    if (target.width == 0 || target.height == 0) {
        return;
    }

    RE::BSGraphics::RendererShadowState* shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();
    if (!shadowState) {
        return;
    }

    RE::BSGraphics::RendererShadowState::FLAT_RUNTIME_DATA& data = shadowState->GetRuntimeData();
    data.viewPort = D3D11_VIEWPORT{
        0.0f,
        0.0f,
        static_cast<float>(target.width),
        static_cast<float>(target.height),
        0.0f,
        1.0f
    };
    data.depthStencil = static_cast<std::uint32_t>(RE::RENDER_TARGETS_DEPTHSTENCIL::kNONE);
    data.depthStencilSlice = 0;
    data.setDepthStencilMode = RE::BSGraphics::SRTM_RESTORE;
    data.stateUpdateFlags |= RE::BSGraphics::DIRTY_RENDERTARGET;
    data.stateUpdateFlags |= RE::BSGraphics::DIRTY_VIEWPORT;
    data.stateUpdateFlags |= RE::BSGraphics::DIRTY_DEPTH_MODE;
    data.stateUpdateFlags |= RE::BSGraphics::DIRTY_DEPTH_STENCILREF_MODE;
}

static void SetRenderTargetCameraFrustum(RE::NiCamera& camera, const RenderTarget& target) {
    if (target.width == 0 || target.height == 0) {
        return;
    }

    RE::NiFrustum& frustum = camera.GetRuntimeData2().viewFrustum;
    float horizontalSpan = frustum.fRight - frustum.fLeft;
    if (horizontalSpan == 0.0f) {
        return;
    }

    if (horizontalSpan < 0.0f) {
        horizontalSpan = -horizontalSpan;
    }

    const float targetAspect = static_cast<float>(target.width) / static_cast<float>(target.height);
    const float verticalSpan = horizontalSpan / targetAspect;
    const float centerY = (frustum.fTop + frustum.fBottom) * 0.5f;

    frustum.fTop = centerY + verticalSpan * 0.5f;
    frustum.fBottom = centerY - verticalSpan * 0.5f;
}

static void RenderMeshOveraly(RE::INTERFACE_LIGHT_SCHEME scheme) {
    using func_t = void(RE::UI3DSceneManager*, RE::INTERFACE_LIGHT_SCHEME, RE::NiCamera*, bool);
    const REL::Relocation<func_t> func{REL::RelocationID(51855, 52727)};

    RE::ImageSpaceManager* imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
    RE::ImageSpaceManager::UNK_BSImagespaceShaderISTemporalAA* temporalAA = nullptr;
    bool wasTAAEnabled = false;

    if (imageSpaceManager) {
        temporalAA = imageSpaceManager->GetRuntimeData().BSImagespaceShaderISTemporalAA;
    }

    if (temporalAA) {
        wasTAAEnabled = temporalAA->taaEnabled;
        temporalAA->taaEnabled = false;
    }

    func(RE::UI3DSceneManager::GetSingleton(), scheme, nullptr, false);

    if (temporalAA) {
        temporalAA->taaEnabled = wasTAAEnabled;
    }
}

static void CreateMenuLight(RE::INTERFACE_LIGHT_SCHEME scheme) {
    auto manager = RE::UI3DSceneManager::GetSingleton();
    auto light = manager->menuLights[(int)scheme];
    auto object = manager->menuObjects[(int)scheme].get();
    using func_t = RE::BSLight*(RE::MenuLight*, RE::INTERFACE_LIGHT_SCHEME, RE::NiNode*);
    const REL::Relocation<func_t> func{REL::RelocationID(51878, 52751)};
    func(light, scheme, object);
}

MeshRenderingFrameworkAPI::Internal::IMesh* RenderManager::AddByNifPAth(const char* nifPath, uint32_t width, uint32_t height) {
    std::unique_lock lock(mutex);
    if (auto mesh = new Mesh(nifPath, width, height)) {
        meshes[mesh->mesh] = mesh;
        return mesh->mesh;
    }

    return nullptr;
}

MeshRenderingFrameworkAPI::Internal::IMesh* RenderManager::AddByNiAVObjectList(RE::NiAVObject* const* objects, uint32_t objectCount, uint32_t width, uint32_t height) {
    std::unique_lock lock(mutex);
    if (auto mesh = new Mesh(objects, objectCount, width, height)) {
        meshes[mesh->mesh] = mesh;
        return mesh->mesh;
    }

    return nullptr;
}

bool RenderManager::CopyRenderTargetToMesh(Mesh* mesh, RenderTarget* target) {
    if (!mesh || !mesh->mesh || !target || !target->texture || !device || !context) {
        return false;
    }

    auto* outputMesh = mesh->mesh;

    D3D11_TEXTURE2D_DESC targetDesc{};
    target->texture->GetDesc(&targetDesc);

    auto needsTexture = outputMesh->texture == nullptr || outputMesh->SRV == nullptr;
    if (outputMesh->texture) {
        D3D11_TEXTURE2D_DESC currentDesc{};
        outputMesh->texture->GetDesc(&currentDesc);

        needsTexture = needsTexture || currentDesc.Width != targetDesc.Width || currentDesc.Height != targetDesc.Height ||
                       currentDesc.MipLevels != targetDesc.MipLevels || currentDesc.ArraySize != targetDesc.ArraySize ||
                       currentDesc.Format != targetDesc.Format ||
                       currentDesc.SampleDesc.Count != targetDesc.SampleDesc.Count ||
                       currentDesc.SampleDesc.Quality != targetDesc.SampleDesc.Quality;
    }

    if (needsTexture) {
        if (outputMesh->SRV) {
            outputMesh->SRV->Release();
            outputMesh->SRV = nullptr;
        }

        if (outputMesh->texture) {
            outputMesh->texture->Release();
            outputMesh->texture = nullptr;
        }

        auto meshTextureDesc = targetDesc;
        meshTextureDesc.Usage = D3D11_USAGE_DEFAULT;
        meshTextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        meshTextureDesc.CPUAccessFlags = 0;
        meshTextureDesc.MiscFlags = 0;

        auto hr = device->CreateTexture2D(&meshTextureDesc, nullptr, &outputMesh->texture);
        if (FAILED(hr)) {
            logger::error("Failed to create mesh texture copy target: {:08X}", static_cast<std::uint32_t>(hr));
            return false;
        }

        hr = device->CreateShaderResourceView(outputMesh->texture, nullptr, &outputMesh->SRV);
        if (FAILED(hr)) {
            logger::error("Failed to create mesh shader resource view: {:08X}", static_cast<std::uint32_t>(hr));
            outputMesh->texture->Release();
            outputMesh->texture = nullptr;
            return false;
        }
    }

    context->CopyResource(outputMesh->texture, target->texture);
    return true;
}



void RenderManager::Render() {
    std::vector<Mesh*> deleteMeshes;

    {
    std::shared_lock lock(mutex);
    auto manager = RE::UI3DSceneManager::GetSingleton();


    auto ui = RE::UI::GetSingleton();
    if (ui->IsApplicationMenuOpen() ||
        ui->IsMenuOpen(RE::BookMenu::MENU_NAME) ||
        ui->IsMenuOpen(RE::MapMenu::MENU_NAME) ||
        ui->IsMenuOpen(RE::MistMenu::MENU_NAME) ||
        ui->IsMenuOpen(RE::MainMenu::MENU_NAME) ||
        ui->IsMenuOpen(RE::StatsMenu::MENU_NAME)) {
        return;
    }

    if (lastLightScheme != manager->currentlightScheme) {
        lastLightScheme = manager->currentlightScheme;
        CreateLights();
    }

    if (meshes.size() == 0) {
        return;
    }

    isRenderingMesh = true;

    auto last_world = manager->camera->world;
    auto last_scheme = manager->currentlightScheme;

    StoreOriginalRenderMeshes();


    manager->currentlightScheme = RE::INTERFACE_LIGHT_SCHEME::kInventory;

    std::map<std::string, std::vector<Mesh*>> meshesByResolution;
    for (auto& [key, mesh] : meshes) {
        if (!mesh->mesh->mustUpdate && !mesh->mesh->alwaysUpdate) {
            continue;
        }
        auto res = RenderTarget::GetKey(mesh->mesh->width, mesh->mesh->height);
        meshesByResolution[res].push_back(mesh);
    }


    for (auto& [res, meshes] : meshesByResolution) {

        if (!renderTarget.contains(res)) {
            auto t = new RenderTarget();
            t->width = meshes[0]->mesh->width;
            t->height = meshes[0]->mesh->height;
            renderTarget[res] = t;
        }

        auto target = renderTarget[res];

        if (!target->initialized) {
            InitRenderTarget(target);
        }

        manager->camera->world.translate.y = cameraZ;

        const auto renderer = RE::BSGraphics::Renderer::GetSingleton();

        if (!renderer || !target|| !target->renderTargetView) {
            return;
        }


        auto& data = renderer->GetRuntimeData();
        auto* context = data.context;

        auto& framebuffer = data.renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];

        auto* targetRTV = reinterpret_cast<REX::W32::ID3D11RenderTargetView*>(target->renderTargetView);

        REX::W32::ID3D11RenderTargetView* oldBoundRTV = nullptr;
        REX::W32::ID3D11DepthStencilView* oldBoundDSV = nullptr;

        context->OMGetRenderTargets(1, &oldBoundRTV, &oldBoundDSV);
        const auto oldRasterizerState = StoreRasterizerState(context);
        const auto oldRendererShadowState = StoreRendererShadowState();
        const auto oldCameraFrustum = manager->camera->GetRuntimeData2().viewFrustum;
        SetRenderTargetCameraFrustum(*manager->camera, *target);

        const auto oldFramebuffer = framebuffer;
        framebuffer.texture = target->texture;
        framebuffer.textureCopy = nullptr;
        framebuffer.RTV = target->renderTargetView;
        framebuffer.SRV = target->SRV;
        framebuffer.SRVCopy = nullptr;
        framebuffer.UAV = nullptr;

        context->OMSetRenderTargets(1, &targetRTV, nullptr);

        REX::W32::ID3D11BlendState* oldBlendState = nullptr;
        float oldBlendFactor[4]{};
        std::uint32_t oldSampleMask{};

        for (auto mesh : meshes) {
            AddMeshesToRender(mesh);
            context->OMGetBlendState(&oldBlendState, oldBlendFactor, &oldSampleMask);
            context->OMSetBlendState(reinterpret_cast<REX::W32::ID3D11BlendState*>(target->blendState), nullptr, 0xFFFFFFFF);
            const float clearColor[4]{0.0f, 0.0f, 0.0f, 0.0f};
            context->ClearRenderTargetView(targetRTV, clearColor);
            context->OMSetBlendState(oldBlendState, oldBlendFactor, oldSampleMask);
            ReleaseIfExists(oldBlendState);

            if (renderQuery) {
                context->Begin(reinterpret_cast<REX::W32::ID3D11Query*>(renderQuery));
            }

            SetRenderTargetRasterizerState(context, *target);
            SetRenderTargetShadowState(*target);
            RenderMeshOveraly(RE::INTERFACE_LIGHT_SCHEME::kInventory);
            SetRenderTargetRasterizerState(context, *target);
            SetRenderTargetShadowState(*target);
            RenderMeshOveraly(RE::INTERFACE_LIGHT_SCHEME::kInventory);
            if (renderQuery) {
                context->End(reinterpret_cast<REX::W32::ID3D11Query*>(renderQuery));
            }
            RemoveRenderedMesh(mesh);
            CopyRenderTargetToMesh(mesh, target);

            UINT64 samplesRendered = 0;

            if (renderQuery) {
                while (context->GetData(reinterpret_cast<REX::W32::ID3D11Query*>(renderQuery), &samplesRendered, sizeof(samplesRendered), 0) == S_FALSE) {
                }
            }
            if (samplesRendered > 0) {
                mesh->mesh->mustUpdate = false;
                if (mesh->mesh->saveNextFrame && mesh->mesh->savePath) {
                    Save(mesh->mesh, mesh->mesh->savePath);
                }
                if (mesh->mesh->deleteAfterSave) {
                    deleteMeshes.push_back(mesh);
                }
            }
        }

        framebuffer = oldFramebuffer;

        RestoreRendererShadowState(oldRendererShadowState);
        manager->camera->GetRuntimeData2().viewFrustum = oldCameraFrustum;
        RestoreRasterizerState(context, oldRasterizerState);
        context->OMSetRenderTargets(1, &oldBoundRTV, oldBoundDSV);
        ReleaseIfExists(oldBoundRTV);
        ReleaseIfExists(oldBoundDSV);
    }

    ReAttachOriginalMeshes();

    manager->currentlightScheme = last_scheme;
    manager->camera->world = last_world;

    isRenderingMesh = false;
    }
    for (auto item : deleteMeshes) {
        IMesh_Delete(item->mesh);
    }
}

void RenderManager::ReAttachOriginalMeshes() {
    auto manager = RE::UI3DSceneManager::GetSingleton();

    for (auto item : originalChildren) {
        if (item) {
            manager->menuObjects[1]->AttachChild(item, false);
        }
    }
}

void RenderManager::RemoveRenderedMesh(Mesh* mesh) {
    auto manager = RE::UI3DSceneManager::GetSingleton();
    manager->DetachChild(mesh->node->obj);
}

void RenderManager::StoreOriginalRenderMeshes() {
    auto manager = RE::UI3DSceneManager::GetSingleton();
    originalChildren.clear();

    for (auto item : manager->menuObjects[1]->children) {
        if (item) {
            originalChildren.push_back(item.get());
            manager->menuObjects[1]->DetachChild(item.get());
        }
    }
}

void RenderManager::AddMeshesToRender(Mesh* mesh) {

    auto manager = RE::UI3DSceneManager::GetSingleton();

    manager->AttachChild(mesh->node->obj);
    mesh->ApplyTransform();
}


void RenderManager::CreateLights() {
    auto manager = RE::UI3DSceneManager::GetSingleton();
    manager->shadowSceneNode->activeLights.clear();
    manager->currentlightScheme = RE::INTERFACE_LIGHT_SCHEME::kInventory;
    CreateMenuLight(RE::INTERFACE_LIGHT_SCHEME::kInventory);
}

bool RenderManager::GetIsRendering() { return isRenderingMesh; }


void RenderManager::InitRenderTarget(RenderTarget* target)
{
    if (!device) {
        return;
    }

    if (target->blendState) {
        target->blendState->Release();
        target->blendState = nullptr;
    }

    if (target->SRV) {
        target->SRV->Release();
        target->SRV = nullptr;
    }

    if (target->renderTargetView) {
        target->renderTargetView->Release();
        target->renderTargetView = nullptr;
    }

    if (target->texture) {
        target->texture->Release();
        target->texture = nullptr;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = target->width;
    desc.Height = target->height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &target->texture);

    if (FAILED(hr)) {
        return;
    }

    hr = device->CreateRenderTargetView(target->texture, nullptr, &target->renderTargetView);

    if (FAILED(hr)) {
        target->texture->Release();
        target->texture = nullptr;
        return;
    }

    hr = device->CreateShaderResourceView(target->texture, nullptr, &target->SRV);

    if (FAILED(hr)) {
        target->renderTargetView->Release();
        target->renderTargetView = nullptr;

        target->texture->Release();
        target->texture = nullptr;
        return;
    }

    D3D11_BLEND_DESC blendDesc{};

    auto& renderTargetBlend = blendDesc.RenderTarget[0];

    renderTargetBlend.BlendEnable = TRUE;
    renderTargetBlend.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    renderTargetBlend.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    renderTargetBlend.BlendOp = D3D11_BLEND_OP_ADD;

    renderTargetBlend.SrcBlendAlpha = D3D11_BLEND_ONE;
    renderTargetBlend.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    renderTargetBlend.BlendOpAlpha = D3D11_BLEND_OP_ADD;

    renderTargetBlend.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = device->CreateBlendState(&blendDesc, &target->blendState);

    if (FAILED(hr)) {
        target->SRV->Release();
        target->SRV = nullptr;

        target->renderTargetView->Release();
        target->renderTargetView = nullptr;

        target->texture->Release();
        target->texture = nullptr;
        return;
    }

    target->initialized  = true;
    return;
}


void RenderManager::Init(ID3D11Device* device, ID3D11DeviceContext* context) {
    RenderManager::device = device;
    RenderManager::context = context;
    ReleaseIfExists(renderQuery);

    D3D11_QUERY_DESC queryDesc{};
    queryDesc.Query = D3D11_QUERY_OCCLUSION;
    if (FAILED(device->CreateQuery(&queryDesc, &renderQuery))) {
        renderQuery = nullptr;
    }
}

void RenderManager::Save(MeshRenderingFrameworkAPI::Internal::IMesh* mesh, const char* filename) {
    std::filesystem::path path(filename);

    ID3D11Resource* resource = nullptr;
    mesh->SRV->GetResource(&resource);

    DirectX::ScratchImage image;
    if (FAILED(DirectX::CaptureTexture(device, context, resource, image))) {
        resource->Release();
        return;
    }

    const auto wideName = path.wstring();

    if (_wcsicmp(path.extension().c_str(), L".dds") == 0) {
        DirectX::SaveToDDSFile(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::DDS_FLAGS_NONE, wideName.c_str());
    } else {
        DirectX::SaveToWICFile(*image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_FORCE_SRGB, GUID_ContainerFormatPng, wideName.c_str());
    }

    resource->Release();
}

std::string RenderTarget::GetKey(uint32_t width, uint32_t height) { return std::format("{}x{}", width, height); }
