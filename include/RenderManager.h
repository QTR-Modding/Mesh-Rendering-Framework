#pragma once
#include <shared_mutex>
#include "Mesh.h"
#include "MeshRenderingFrameworkAPI.h"

struct RenderTarget {
    ID3D11Texture2D* texture = nullptr;
    ID3D11RenderTargetView* renderTargetView = nullptr;
    ID3D11ShaderResourceView* SRV = nullptr;
    ID3D11BlendState* blendState = nullptr;
    uint32_t numReferences = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool initialized = false;
    ~RenderTarget() {
        if (blendState) {
            blendState->Release();
        }

        if (SRV) {
            SRV->Release();
        }

        if (renderTargetView) {
            renderTargetView->Release();
        }

        if (texture) {
            texture->Release();
        }
    }
    static std::string GetKey(uint32_t width, uint32_t height);
};

class RenderManager {
    static inline ID3D11Device* device;
    static inline ID3D11DeviceContext* context;
    static inline ID3D11Query* renderQuery = nullptr;

    static inline RE::INTERFACE_LIGHT_SCHEME lastLightScheme = RE::INTERFACE_LIGHT_SCHEME::kTotal;
    static inline bool isRenderingMesh = false;
    static inline std::vector<RE::NiAVObject*> originalChildren;
    static void CreateLights();
    static void StoreOriginalRenderMeshes();
    static void ReAttachOriginalMeshes();
    static void RemoveRenderedMesh(Mesh* mesh);
    static void AddMeshesToRender(Mesh* mesh);
    static bool CopyRenderTargetToMesh(Mesh* mesh, RenderTarget* target);

public:

    static MeshRenderingFrameworkAPI::Internal::IMesh* AddByNifPAth(const char* nifPath, uint32_t width, uint32_t height);

    static void Render();
    static bool GetIsRendering();
    static void InitRenderTarget(RenderTarget* target);
    static void Init(ID3D11Device* device, ID3D11DeviceContext* context);

    static void Save(MeshRenderingFrameworkAPI::Internal::IMesh* mesh, const char* filename);

    static inline std::map<std::string, RenderTarget*> renderTarget;
    static inline std::map<uint64_t, Mesh*> meshes;
    static inline std::shared_mutex mutex;
};
