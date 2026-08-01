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


void IMesh_Delete(MeshRenderingFrameworkAPI::Internal::IMesh* mesh) { 
    if (!mesh) {
        return;
    }

     std::unique_lock lock(RenderManager::mutex);

    std::map<std::string, RenderTarget*>::iterator renderTargetIt;
    renderTargetIt = RenderManager::renderTarget.find(RenderTarget::GetKey(mesh->width, mesh->height));
    if (renderTargetIt != RenderManager::renderTarget.end()) {
        renderTargetIt->second->numReferences--;
        if (renderTargetIt->second->numReferences <= 0) {
            delete renderTargetIt->second;
            RenderManager::renderTarget.erase(renderTargetIt);
        }
    }

	std::map<MeshRenderingFrameworkAPI::Internal::IMesh*, Mesh*>::iterator it;
    it = RenderManager::meshes.find(mesh);
    if (it != RenderManager::meshes.end()) {
        auto item = it->second;
        RenderManager::meshes.erase(it);
        delete item;
    }

}

FUNCTION_PREFIX MeshRenderingFrameworkAPI::Internal::IMesh* IMesh_Save(
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh,
    const char* filePath)
{
    if (!mesh || !filePath) {
        return nullptr;
    }

    RenderManager::Save(mesh, filePath);
    return mesh;
}
