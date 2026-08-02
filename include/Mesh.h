#pragma once
#include "MeshRenderingFrameworkAPI.h"

#include <array>

struct MeshVertex {
    float position[3]{};
    float normal[3]{};
    float tangent[4]{1.0f, 0.0f, 0.0f, 1.0f};
    float uv[2]{};
    float color[4]{1.0f, 1.0f, 1.0f, 1.0f};
};

enum class MeshTextureSlot : std::size_t {
    Diffuse,
    Normal,
    Glow,
    Height,
    Environment,
    EnvironmentMask,
    Subsurface,
    Backlight,
    Specular,
    FaceTint,
    Count
};

inline constexpr std::size_t MeshTextureSlotCount = static_cast<std::size_t>(MeshTextureSlot::Count);

enum class MeshAlphaMode {
    Opaque,
    Cutout,
    Blend
};

struct MeshMaterialConstants {
    float alphaThreshold = -1.0f;
    float materialAlpha = 1.0f;
    float blendEnabled = 0.0f;
    float specularStrength = 0.0f;
    float specularColor[3]{1.0f, 1.0f, 1.0f};
    float glossiness = 16.0f;
    float emissiveColor[3]{};
    float emissiveMultiple = 0.0f;
    float tintColor[3]{1.0f, 1.0f, 1.0f};
    float environmentScale = 0.0f;
    float uvScale[2]{1.0f, 1.0f};
    float uvOffset[2]{};
    float parallaxScale = 0.0f;
    float backlightPower = 0.0f;
    float rimlightPower = 2.0f;
    float auxiliaryMapMode = 0.0f;
    float hasNormalMap = 0.0f;
    float hasHeightMap = 0.0f;
    float hasEnvironmentMap = 0.0f;
    float hasEnvironmentMask = 0.0f;
    float hasSubsurfaceMap = 0.0f;
    float hasBacklightMap = 0.0f;
    float hasSpecularMap = 0.0f;
    float modelSpaceNormals = 0.0f;
    float useVertexColors = 1.0f;
    float useVertexAlpha = 1.0f;
    float emissiveEnabled = 0.0f;
    float specularEnabled = 1.0f;
    float hasAuxiliaryMap = 0.0f;
    float hasFaceTintMap = 0.0f;
    float faceOrSkin = 0.0f;
    float hairMaterial = 0.0f;
};

static_assert(sizeof(MeshMaterialConstants) % 16 == 0);

struct MeshPart {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint16_t> indices;
    std::array<std::string, MeshTextureSlotCount> texturePaths;
    ID3D11Buffer* vertexBuffer = nullptr;
    ID3D11Buffer* indexBuffer = nullptr;
    std::array<ID3D11ShaderResourceView*, MeshTextureSlotCount> textureViews{};
    std::array<bool, MeshTextureSlotCount> textureLoadAttempted{};
    MeshAlphaMode alphaMode = MeshAlphaMode::Opaque;
    float alphaThreshold = -1.0f;
    float materialAlpha = 1.0f;
    float specularColor[3]{1.0f, 1.0f, 1.0f};
    float specularStrength = 1.0f;
    float glossiness = 16.0f;
    float emissiveColor[3]{};
    float emissiveMultiple = 1.0f;
    float tintColor[3]{1.0f, 1.0f, 1.0f};
    float environmentScale = 1.0f;
    float uvScale[2]{1.0f, 1.0f};
    float uvOffset[2]{};
    float parallaxScale = 0.0f;
    float backlightPower = 0.0f;
    float rimlightPower = 2.0f;
    float auxiliaryMapMode = 0.0f;
    bool modelSpaceNormals = false;
    bool useVertexColors = true;
    bool useVertexAlpha = true;
    bool emissiveEnabled = false;
    bool specularEnabled = true;
    bool environmentEnabled = false;
    bool faceOrSkin = false;
    bool hairMaterial = false;
    RE::NiPoint3 center{};

    MeshPart() = default;
    MeshPart(const MeshPart&) = delete;
    MeshPart& operator=(const MeshPart&) = delete;
    MeshPart(MeshPart&& other) noexcept;
    MeshPart& operator=(MeshPart&& other) noexcept;
    ~MeshPart();

    void ResetGpuResources();
};

class Mesh {
private:
    static inline uint64_t autoIncrement = 0;
    float boundingRadius = 0.0f;
    void Fit(RE::NiPoint2 position, RE::NiPoint2 scale);
    void Initialize(uint32_t width, uint32_t height);
    bool Load(const char* nifPath);

public:
    Mesh(const char* nifPath, uint32_t width, uint32_t height);
    Mesh(
        const char* const* basePaths,
        uint32_t basePathCount,
        const char* const* attachmentPaths,
        uint32_t attachmentPathCount,
        uint32_t width,
        uint32_t height);
    ~Mesh();

    MeshRenderingFrameworkAPI::Internal::IMesh* mesh = nullptr;
    std::string sourcePath;
    std::vector<MeshPart> parts;

    bool IsValid() const;
    bool InitializeGpuResources(ID3D11Device* device);
    void ResetGpuResources();
    void Draw(
        ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* const* fallbackTextures,
        ID3D11Buffer* materialConstantBuffer,
        bool transparentPass) const;
};
