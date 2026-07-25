#include "Mesh.h"
#include "RenderManager.h"
#include "MeshMath.h"
#include "Geometry.h"
void Mesh::Fit(RE::NiPoint2 position, RE::NiPoint2 scale) {
    const RE::NiPoint2 renderSize{
        static_cast<float>(mesh->width),
        static_cast<float>(mesh->height)
    };
    mesh->position = MeshMath::ToWorldPosition(MeshMath::PositionToScreenRatio(position + scale / 2, renderSize), renderSize);
    mesh->scale = MeshMath::ScaleToRect(mesh, scale, renderSize);
}

void Mesh::Initialize(uint32_t width, uint32_t height) {
    node = new Node();
    nif = nullptr;
    objectListNode = nullptr;
    mesh = new MeshRenderingFrameworkAPI::Internal::IMesh();
    mesh->width = width;
    mesh->height = height;
    mesh->id = autoIncrement++;
}

void Mesh::AttachAndFit(RE::NiAVObject* object, RE::NiAVObject* objectToCenter, uint32_t width, uint32_t height) {
    if (!object || !objectToCenter) {
        return;
    }

    node->node->AttachChild(object, false);
    node->Update();

    Geometry geo(object);
    auto bound = geo.GetBoundingBox({0, 0, 0}, object->local.scale);
    mesh->boundMin = bound.first;
    mesh->boundMax = bound.second;
    //logger::info("min {} {} {}", mesh->boundMin.x, mesh->boundMin.y, mesh->boundMin.z);
    //logger::info("max {} {} {}", mesh->boundMax.x, mesh->boundMax.y, mesh->boundMax.z);
    auto center = (bound.first + bound.second) * 0.5;
    objectToCenter->local.translate -= center;
    Fit(RE::NiPoint2{0.f, 0.f}, RE::NiPoint2{(float)width, (float)height});
}

Mesh::Mesh(const char* nifPath, uint32_t width, uint32_t height) {
    Initialize(width, height);

    nif = Nif::Create(nifPath, true);
    if (!nif) {
        return;
    }

    AttachAndFit(nif->obj, nif->obj, width, height);
}

Mesh::Mesh(RE::NiAVObject* const* objects, uint32_t objectCount, uint32_t width, uint32_t height) {
    Initialize(width, height);

    if (!objects || objectCount == 0) {
        return;
    }

    objectListNode = new Node();
    uint32_t attachedObjects = 0;

    for (uint32_t i = 0; i < objectCount; ++i) {
        auto object = objects[i];
        if (!object) {
            continue;
        }
        auto clone = static_cast<RE::NiAVObject*>(object->Clone());
        if (!clone) {
            continue;
        }
        objectListNode->node->AttachChild(clone, false);
        attachedObjects++;
    }

    if (attachedObjects == 0) {
        return;
    }

    AttachAndFit(objectListNode->obj, objectListNode->obj, width, height);
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
    delete objectListNode;
    delete node;
    delete mesh;
}


void Mesh::ApplyTransform() {
    node->Update(mesh->rotation, mesh->position, mesh->scale);
}

