#pragma once
#include "Node.h"
#include "Nif.h"
#include "MeshRenderingFrameworkAPI.h"
class Mesh {
private:
    static inline uint64_t autoIncrement = 0;
    void Fit(RE::NiPoint2 position, RE::NiPoint2 scale);

public:
    Mesh(const char* nifPath, uint32_t width, uint32_t height);
    ~Mesh();

    Nif* nif;
    Node* node;
    MeshRenderingFrameworkAPI::Internal::IMesh* mesh;

    void ApplyTransform();
};
