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
FUNCTION_PREFIX void IMesh_Delete(MeshRenderingFrameworkAPI::Internal::IMesh* mesh);
FUNCTION_PREFIX MeshRenderingFrameworkAPI::Internal::IMesh* IMesh_Save(MeshRenderingFrameworkAPI::Internal::IMesh* mesh, const char* filePath);
