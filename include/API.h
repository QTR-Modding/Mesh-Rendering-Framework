#pragma once

#include "MeshRenderingFrameworkAPI.h"

#define FUNCTION_PREFIX extern "C" [[maybe_unused]] __declspec(dllexport)

FUNCTION_PREFIX MeshRenderingFrameworkAPI::Internal::IMesh* IMesh_CreateByNifPath(const char* nifPath, uint32_t width, uint32_t height);
FUNCTION_PREFIX MeshRenderingFrameworkAPI::Internal::IMesh* IMesh_CreateByNifPathSet(
    const char* const* basePaths,
    uint32_t basePathCount,
    const char* const* attachmentPaths,
    uint32_t attachmentPathCount,
    uint32_t width,
    uint32_t height);
FUNCTION_PREFIX MeshRenderingFrameworkAPI::Internal::IMesh* IMesh_CreateByNiAVObjectList(RE::NiAVObject* const* objects, uint32_t objectCount, uint32_t width, uint32_t height);
FUNCTION_PREFIX bool IMesh_SetBoneLocalPose(
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
    const char* const* boneNames,
    const std::int16_t* parentIndices,
    const MeshRenderingFrameworkAPI::BoneTransform* transforms,
    uint32_t transformCount);
FUNCTION_PREFIX bool IMesh_PlayAnimation(
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
    const char* animationPath,
    const char* skeletonPath,
    bool loop);
FUNCTION_PREFIX bool IMesh_SetFaceMorphSource(
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
    RE::Actor* actor);
FUNCTION_PREFIX bool IMesh_SetMorph(
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
    const char* triPath,
    const char* morphName,
    float value);
FUNCTION_PREFIX bool IMesh_ClearFaceMorphs(
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh);
FUNCTION_PREFIX bool IMesh_SetTextureSet(
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
    const char* nifPath,
    const char* const* texturePaths,
    std::uint32_t texturePathCount,
    bool modelSpaceNormals,
    bool includeBodyShape);
FUNCTION_PREFIX std::uint32_t IMesh_GetLightCount(MeshRenderingFrameworkAPI::Internal::IMesh* mesh);
FUNCTION_PREFIX MeshRenderingFrameworkAPI::Internal::ILight* IMesh_GetLight(
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
    std::uint32_t lightIndex);
FUNCTION_PREFIX MeshRenderingFrameworkAPI::Internal::ILight* IMesh_AddLight(
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
    float directionX,
    float directionY,
    float directionZ,
    float red,
    float green,
    float blue,
    float strength);
FUNCTION_PREFIX bool IMesh_ClearLights(MeshRenderingFrameworkAPI::Internal::IMesh* mesh);
FUNCTION_PREFIX bool IMesh_SetExposure(
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
    float exposure);
FUNCTION_PREFIX bool IMesh_GetExposure(
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
    float* exposure);
FUNCTION_PREFIX bool ILight_SetDirection(
    MeshRenderingFrameworkAPI::Internal::ILight* light,
    float x,
    float y,
    float z);
FUNCTION_PREFIX bool ILight_GetDirection(
    MeshRenderingFrameworkAPI::Internal::ILight* light,
    float* x,
    float* y,
    float* z);
FUNCTION_PREFIX bool ILight_SetColor(
    MeshRenderingFrameworkAPI::Internal::ILight* light,
    float red,
    float green,
    float blue);
FUNCTION_PREFIX bool ILight_GetColor(
    MeshRenderingFrameworkAPI::Internal::ILight* light,
    float* red,
    float* green,
    float* blue);
FUNCTION_PREFIX bool ILight_SetStrength(
    MeshRenderingFrameworkAPI::Internal::ILight* light,
    float strength);
FUNCTION_PREFIX bool ILight_GetStrength(
    MeshRenderingFrameworkAPI::Internal::ILight* light,
    float* strength);
FUNCTION_PREFIX void IMesh_Delete(MeshRenderingFrameworkAPI::Internal::IMesh* mesh);
FUNCTION_PREFIX MeshRenderingFrameworkAPI::Internal::IMesh* IMesh_Save(MeshRenderingFrameworkAPI::Internal::IMesh* mesh, const char* filePath);
