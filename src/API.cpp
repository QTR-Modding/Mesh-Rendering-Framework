#include "API.h"
#include "RenderManager.h"

namespace
{
    void AddRenderTargetReference(uint32_t width, uint32_t height)
    {
        const auto key = RenderTarget::GetKey(width, height);

        std::unique_lock lock(RenderManager::mutex);
        auto renderTargetIt = RenderManager::renderTarget.find(key);
        if (renderTargetIt != RenderManager::renderTarget.end()) {
            renderTargetIt->second->numReferences++;
        } else {
            auto target = new RenderTarget();
            target->width = width;
            target->height = height;
            target->numReferences = 1;
            RenderManager::renderTarget[key] = target;
        }
    }
}

MeshRenderingFrameworkAPI::Internal::IMesh* IMesh_CreateByNifPath(const char* nifPath, uint32_t width, uint32_t height) {
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh = RenderManager::AddByNifPAth(nifPath, width, height);
    if (mesh) {
        AddRenderTargetReference(width, height);
        if (!RenderManager::Render(mesh)) {
            ::IMesh_Delete(mesh);
            return nullptr;
        }
    }
    return mesh;
}

MeshRenderingFrameworkAPI::Internal::IMesh* IMesh_CreateByNifPathSet(
    const char* const* basePaths,
    uint32_t basePathCount,
    const char* const* attachmentPaths,
    uint32_t attachmentPathCount,
    uint32_t width,
    uint32_t height)
{
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh = RenderManager::AddByNifPathSet(
        basePaths,
        basePathCount,
        attachmentPaths,
        attachmentPathCount,
        width,
        height);
    if (mesh) {
        AddRenderTargetReference(width, height);
        if (!RenderManager::Render(mesh)) {
            ::IMesh_Delete(mesh);
            return nullptr;
        }
    }
    return mesh;
}

MeshRenderingFrameworkAPI::Internal::IMesh* IMesh_CreateByNiAVObjectList(RE::NiAVObject* const* objects, uint32_t objectCount, uint32_t width, uint32_t height) {
    if (!objects || objectCount == 0) {
        return nullptr;
    }

    bool hasObject = false;
    for (uint32_t i = 0; i < objectCount; ++i) {
        if (objects[i]) {
            hasObject = true;
            break;
        }
    }

    if (!hasObject) {
        return nullptr;
    }

    // nifly operates on serialized NIF streams and cannot reconstruct a source
    // file from arbitrary live Creation Engine scene objects.
    return RenderManager::AddByNiAVObjectList(objects, objectCount, width, height);
}

bool IMesh_SetBoneLocalPose(
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
    const char* const* boneNames,
    const std::int16_t* parentIndices,
    const MeshRenderingFrameworkAPI::BoneTransform* transforms,
    uint32_t transformCount)
{
    return RenderManager::SetBoneLocalPose(
        mesh,
        boneNames,
        parentIndices,
        transforms,
        transformCount);
}

bool IMesh_PlayAnimation(
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
    const char* animationPath,
    const char* skeletonPath,
    bool loop)
{
    return RenderManager::PlayAnimation(mesh, animationPath, skeletonPath, loop);
}

bool IMesh_SetFaceMorphSource(
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
    RE::Actor* actor)
{
    return RenderManager::SetFaceMorphSource(mesh, actor);
}

bool IMesh_SetMorph(
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
    const char* triPath,
    const char* morphName,
    float value)
{
    return RenderManager::SetMorph(mesh, triPath, morphName, value);
}

bool IMesh_ClearFaceMorphs(
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh)
{
    return RenderManager::ClearFaceMorphs(mesh);
}

bool IMesh_SetTextureSet(
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
    const char* nifPath,
    const char* const* texturePaths,
    std::uint32_t texturePathCount,
    bool modelSpaceNormals,
    bool includeBodyShape)
{
    return RenderManager::SetTextureSet(
        mesh,
        nifPath,
        texturePaths,
        texturePathCount,
        modelSpaceNormals,
        includeBodyShape);
}

std::uint32_t IMesh_GetLightCount(MeshRenderingFrameworkAPI::Internal::IMesh* mesh)
{
    return RenderManager::GetLightCount(mesh);
}

MeshRenderingFrameworkAPI::Internal::ILight* IMesh_GetLight(
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
    std::uint32_t lightIndex)
{
    return RenderManager::GetLight(mesh, lightIndex);
}

MeshRenderingFrameworkAPI::Internal::ILight* IMesh_AddLight(
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
    float directionX,
    float directionY,
    float directionZ,
    float red,
    float green,
    float blue,
    float strength)
{
    return RenderManager::AddLight(
        mesh,
        directionX,
        directionY,
        directionZ,
        red,
        green,
        blue,
        strength);
}

bool IMesh_ClearLights(MeshRenderingFrameworkAPI::Internal::IMesh* mesh)
{
    return RenderManager::ClearLights(mesh);
}

bool IMesh_SetExposure(MeshRenderingFrameworkAPI::Internal::IMesh* mesh, float exposure)
{
    return RenderManager::SetExposure(mesh, exposure);
}

bool IMesh_GetExposure(MeshRenderingFrameworkAPI::Internal::IMesh* mesh, float* exposure)
{
    return RenderManager::GetExposure(mesh, exposure);
}

bool ILight_SetDirection(
    MeshRenderingFrameworkAPI::Internal::ILight* light,
    float x,
    float y,
    float z)
{
    return RenderManager::SetLightDirection(light, x, y, z);
}

bool ILight_GetDirection(
    MeshRenderingFrameworkAPI::Internal::ILight* light,
    float* x,
    float* y,
    float* z)
{
    return RenderManager::GetLightDirection(light, x, y, z);
}

bool ILight_SetColor(
    MeshRenderingFrameworkAPI::Internal::ILight* light,
    float red,
    float green,
    float blue)
{
    return RenderManager::SetLightColor(light, red, green, blue);
}

bool ILight_GetColor(
    MeshRenderingFrameworkAPI::Internal::ILight* light,
    float* red,
    float* green,
    float* blue)
{
    return RenderManager::GetLightColor(light, red, green, blue);
}

bool ILight_SetStrength(MeshRenderingFrameworkAPI::Internal::ILight* light, float strength)
{
    return RenderManager::SetLightStrength(light, strength);
}

bool ILight_GetStrength(MeshRenderingFrameworkAPI::Internal::ILight* light, float* strength)
{
    return RenderManager::GetLightStrength(light, strength);
}

void IMesh_Delete(MeshRenderingFrameworkAPI::Internal::IMesh* mesh) { 
    RenderManager::Delete(mesh);
}

FUNCTION_PREFIX MeshRenderingFrameworkAPI::Internal::IMesh* IMesh_Save(
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
    const char* filePath)
{
    if (!mesh || !filePath) {
        return nullptr;
    }

    return RenderManager::Save(mesh, filePath) ? mesh : nullptr;
}
