#pragma once
#include "Node.h"
#include "Nif.h"
#include "MeshRenderingFrameworkAPI.h"
class Mesh {
private:
    static inline uint64_t autoIncrement = 0;
    void Fit(RE::NiPoint2 position, RE::NiPoint2 scale);
    void Initialize(uint32_t width, uint32_t height);
    void AttachAndFit(RE::NiAVObject* object, RE::NiAVObject* objectToCenter, uint32_t width, uint32_t height);

public:
    Mesh(const char* nifPath, uint32_t width, uint32_t height);
    Mesh(RE::NiAVObject* const* objects, uint32_t objectCount, uint32_t width, uint32_t height);
    ~Mesh();

    Nif* nif;
    Node* node;
    Node* objectListNode;
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh;

    void ApplyTransform();
};
