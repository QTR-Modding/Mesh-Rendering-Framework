#include "API.h"
#include "RenderManager.h"

MeshRenderingFrameworkAPI::Internal::IMesh* IMesh_CreateByNifPath(const char* nifPath, uint32_t width, uint32_t height) { 
    std::map<std::string, RenderTarget*>::iterator renderTargetIt;
    {
        std::shared_lock lock(RenderManager::mutex);
        renderTargetIt = RenderManager::renderTarget.find(RenderTarget::GetKey(width, height));
    }
    if (renderTargetIt != RenderManager::renderTarget.end()) {
        std::shared_lock lock(RenderManager::mutex);
        renderTargetIt->second->numReferences++;
    } else {
        auto target = new RenderTarget();
        target->width = width;
        target->height = height;
        target->numReferences = 1;
        RenderManager::renderTarget[RenderTarget::GetKey(width, height)] = target;
    }

	return RenderManager::AddByNifPAth(nifPath, width, height); 
}


void IMesh_Delete(MeshRenderingFrameworkAPI::Internal::IMesh* mesh) { 
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

	std::map<uint64_t, Mesh*>::iterator it;
    it = RenderManager::meshes.find(mesh->id);
    if (it != RenderManager::meshes.end()) {
        auto item = it->second;
        RenderManager::meshes.erase(it);
        delete item;
    }

}

FUNCTION_PREFIX void IMesh_Save(MeshRenderingFrameworkAPI::Internal::IMesh* mesh, const char* filePath) { 
    RenderManager::Save(mesh, filePath); }
