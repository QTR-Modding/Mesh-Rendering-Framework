#pragma once

#include "MeshRenderingFrameworkAPI.h"

#define FUNCTION_PREFIX extern "C" [[maybe_unused]] __declspec(dllexport)

FUNCTION_PREFIX MeshRenderingFrameworkAPI::Internal::IMesh* IMesh_CreateByNifPath(const char* nifPath, uint32_t width, uint32_t height);
FUNCTION_PREFIX MeshRenderingFrameworkAPI::Internal::IMesh* IMesh_CreateByNiAVObjectList(RE::NiAVObject* const* objects, uint32_t objectCount, uint32_t width, uint32_t height);
FUNCTION_PREFIX void IMesh_Delete(MeshRenderingFrameworkAPI::Internal::IMesh* mesh);
FUNCTION_PREFIX void IMesh_Save(MeshRenderingFrameworkAPI::Internal::IMesh* mesh, const char* filePath);
