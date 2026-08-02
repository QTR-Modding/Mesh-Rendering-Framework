#pragma once
#include <shared_mutex>
#include "Mesh.h"
#include "MeshRenderingFrameworkAPI.h"

struct RenderTarget {
    ID3D11Texture2D* texture = nullptr;
    ID3D11RenderTargetView* renderTargetView = nullptr;
    ID3D11Texture2D* depthTexture = nullptr;
    ID3D11DepthStencilView* depthStencilView = nullptr;
    uint32_t numReferences = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool initialized = false;
    ~RenderTarget() {
        if (depthStencilView) {
            depthStencilView->Release();
        }
        if (depthTexture) {
            depthTexture->Release();
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
    static inline ID3D11DeviceContext* immediateContext;
    static inline ID3D11DeviceContext* renderContext;
    static inline ID3D11Query* completionQuery;
    static inline ID3D11VertexShader* vertexShader = nullptr;
    static inline ID3D11PixelShader* pixelShader = nullptr;
    static inline ID3D11InputLayout* inputLayout = nullptr;
    static inline ID3D11Buffer* constantBuffer = nullptr;
    static inline ID3D11Buffer* materialConstantBuffer = nullptr;
    static inline ID3D11SamplerState* samplerState = nullptr;
    static inline ID3D11RasterizerState* rasterizerState = nullptr;
    static inline ID3D11BlendState* opaqueBlendState = nullptr;
    static inline ID3D11BlendState* alphaBlendState = nullptr;
    static inline ID3D11DepthStencilState* depthWriteState = nullptr;
    static inline ID3D11DepthStencilState* depthReadState = nullptr;
    static inline ID3D11ShaderResourceView* fallbackWhiteTexture = nullptr;
    static inline ID3D11ShaderResourceView* fallbackNormalTexture = nullptr;
    static inline ID3D11ShaderResourceView* fallbackBlackTexture = nullptr;
    static inline ID3D11ShaderResourceView* fallbackEnvironmentTexture = nullptr;

    static bool InitializePipeline();
    static void ReleasePipeline();
    static bool RenderMesh(Mesh* mesh, RenderTarget* target);
    static bool CopyRenderTargetToMesh(Mesh* mesh, RenderTarget* target);
    static bool ExecuteCommands();
    static bool RenderLocked(MeshRenderingFrameworkAPI::Internal::IMesh* mesh);
    static bool SaveLocked(MeshRenderingFrameworkAPI::Internal::IMesh* mesh, const char* filename);
    static void DeleteLocked(MeshRenderingFrameworkAPI::Internal::IMesh* mesh);

public:

    static MeshRenderingFrameworkAPI::Internal::IMesh* AddByNifPAth(const char* nifPath, uint32_t width, uint32_t height);
    static MeshRenderingFrameworkAPI::Internal::IMesh* AddByNifPathSet(
        const char* const* basePaths,
        uint32_t basePathCount,
        const char* const* attachmentPaths,
        uint32_t attachmentPathCount,
        uint32_t width,
        uint32_t height);
    static MeshRenderingFrameworkAPI::Internal::IMesh* AddByNiAVObjectList(RE::NiAVObject* const* objects, uint32_t objectCount, uint32_t width, uint32_t height);
    static bool SetBoneLocalPose(
        MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
        const char* const* boneNames,
        const std::int16_t* parentIndices,
        const MeshRenderingFrameworkAPI::BoneTransform* transforms,
        uint32_t transformCount);
    static bool PlayAnimation(
        MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
        const char* animationPath,
        const char* skeletonPath,
        bool loop);
    static bool SetFaceMorphSource(
        MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
        RE::Actor* actor);
    static bool SetMorph(
        MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
        const char* triPath,
        const char* morphName,
        float value);
    static bool ClearFaceMorphs(
        MeshRenderingFrameworkAPI::Internal::IMesh* mesh);
    static bool SetTextureSet(
        MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
        const char* nifPath,
        const char* const* texturePaths,
        std::uint32_t texturePathCount,
        bool modelSpaceNormals,
        bool includeBodyShape);

    static bool Render(MeshRenderingFrameworkAPI::Internal::IMesh* mesh);
    static void RenderPending();
    static void Delete(MeshRenderingFrameworkAPI::Internal::IMesh* mesh);
    static void InitRenderTarget(RenderTarget* target);
    static bool Init(ID3D11Device* device, ID3D11DeviceContext* context);

    static bool Save(MeshRenderingFrameworkAPI::Internal::IMesh* mesh, const char* filename);

    static inline std::map<std::string, RenderTarget*> renderTarget;
    static inline std::map<MeshRenderingFrameworkAPI::Internal::IMesh*, Mesh*> meshes;
    static inline std::shared_mutex mutex;
};
