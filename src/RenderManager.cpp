#include "RenderManager.h"
#include <d3d11.h>
#include <DirectXTex.h>
#include <wincodec.h>
constexpr float cameraZ = 320;

template <class T>
static void ReleaseIfExists(T*& resource) {
    if (resource) {
        resource->Release();
        resource = nullptr;
    }
}

static void RenderMeshOveraly(RE::INTERFACE_LIGHT_SCHEME scheme) {
    using func_t = void(RE::UI3DSceneManager*, RE::INTERFACE_LIGHT_SCHEME, RE::NiCamera*, bool);
    const REL::Relocation<func_t> func{REL::RelocationID(51855, 52727)};
    func(RE::UI3DSceneManager::GetSingleton(), scheme, nullptr, false);
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

    if (meshes.size() == 0) {
        return;
    }

    auto ui = RE::UI::GetSingleton();
    if (ui->IsApplicationMenuOpen() || ui->IsMenuOpen(RE::MapMenu::MENU_NAME) || ui->IsMenuOpen(RE::MistMenu::MENU_NAME) || ui->IsMenuOpen(RE::MainMenu::MENU_NAME)) {
        return;
    }

    auto manager = RE::UI3DSceneManager::GetSingleton();

    isRenderingMesh = true;

    auto last_world = manager->camera->world;
    auto last_scheme = manager->currentlightScheme;

    StoreOriginalRenderMeshes();

    if (lastLightScheme != manager->currentlightScheme) {
        lastLightScheme = manager->currentlightScheme;
        CreateLights();
    } else {
        manager->currentlightScheme = RE::INTERFACE_LIGHT_SCHEME::kInventory;
    }

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

        auto* oldFramebufferRTV = framebuffer.RTV;

        framebuffer.RTV = target->renderTargetView;

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

            RenderMeshOveraly(RE::INTERFACE_LIGHT_SCHEME::kInventory);
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

        framebuffer.RTV = oldFramebufferRTV;

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
