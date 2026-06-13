#include "Mesh.h"
#include "RenderManager.h"
#include "MeshMath.h"
#include "Geometry.h"
void Mesh::Fit(RE::NiPoint2 position, RE::NiPoint2 scale) {
    mesh->position = MeshMath::ToWorldPosition(MeshMath::PositionToScreenRatio(position + scale / 2));
    mesh->scale = MeshMath::ScaleToRect(mesh, scale);
}
Mesh::Mesh(const char* nifPath, uint32_t width, uint32_t height) {
    node = new Node();
    mesh = new MeshRenderingFrameworkAPI::Internal::IMesh();
    mesh->width = width;
    mesh->height = height;
    mesh->id = autoIncrement++;
    nif = Nif::Create(nifPath, true);
    if (!nif) {
        return;
    }
    node->node->AttachChild(nif->obj, false);
    node->Update();

    Geometry geo(nif->obj);
    auto bound = geo.GetBoundingBox({0, 0, 0}, nif->obj->local.scale);
    mesh->boundMin = bound.first;
    mesh->boundMax = bound.second;
    //logger::info("min {} {} {}", mesh->boundMin.x, mesh->boundMin.y, mesh->boundMin.z);
    //logger::info("max {} {} {}", mesh->boundMax.x, mesh->boundMax.y, mesh->boundMax.z);
    auto center = (bound.first + bound.second) * 0.5;
    nif->obj->local.translate -= center;
    Fit(RE::NiPoint2{0.f, 0.f}, RE::NiPoint2{(float)width, (float)height});
}

Mesh::~Mesh() {
    if (mesh) {
        if (mesh->SRV) {
            mesh->SRV->Release();
            mesh->SRV = nullptr;
        }

        if (mesh->texture) {
            mesh->texture->Release();
            mesh->texture = nullptr;
        }
    }

    delete nif;
    delete node;
    delete mesh;
}


void Mesh::ApplyTransform() {
    node->Update(mesh->rotation, mesh->position, mesh->scale);
}

