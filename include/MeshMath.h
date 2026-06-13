#pragma once
#include "MeshRenderingFrameworkAPI.h"
namespace MeshMath {
    inline RE::NiPoint3 ToWorldPosition(RE::NiPoint2 position) {
        auto screenSize = RE::BSGraphics::Renderer::GetScreenSize();
        float aspect = static_cast<float>(screenSize.width) / static_cast<float>(screenSize.height);
        float refAspect = 16.0f / 9.0f;
        float yCorrection = refAspect / aspect;
        return {-position.x * 130.0f, -500.0f, -position.y * 73.0f * yCorrection};
    }

    inline RE::NiPoint2 WorldToScreen(const RE::NiPoint3& worldPos) {
        auto screenSize = RE::BSGraphics::Renderer::GetScreenSize();
        float aspect = static_cast<float>(screenSize.width) / static_cast<float>(screenSize.height);
        float refAspect = 16.0f / 9.0f;
        float yCorrection = aspect / refAspect;
        float xSize = static_cast<float>(screenSize.width) / 3840 * 15;
        float ySize = static_cast<float>(screenSize.height) / 2160 * 15;
        return {worldPos.x * xSize, worldPos.z * ySize * yCorrection};
    }

    inline RE::NiPoint2 FromWorldPosition(RE::NiPoint3 worldPosition) {
        auto screenSize = RE::BSGraphics::Renderer::GetScreenSize();
        float aspect = static_cast<float>(screenSize.width) / static_cast<float>(screenSize.height);
        float refAspect = 16.0f / 9.0f;
        float yCorrection = refAspect / aspect;

        float x = -worldPosition.x / 130.0f;
        float y = -worldPosition.z / (73.0f * yCorrection);

        return {x, y};
    }

    inline RE::NiPoint2 PositionToScreenRatio(RE::NiPoint2 position) {
        auto screenSize = RE::BSGraphics::Renderer::GetScreenSize();
        return RE::NiPoint2((position.x / (float)screenSize.width) * 2.0f - 1.0f, (position.y / (float)screenSize.height) * 2.0f - 1.0f);
    }

    inline float ScaleToRect(MeshRenderingFrameworkAPI::Internal::IMesh* mesh, RE::NiPoint2 rectSize) {
        RE::NiPoint2 screenMin = WorldToScreen(mesh->boundMin);
        RE::NiPoint2 screenMax = WorldToScreen(mesh->boundMax);

        float meshPixelWidth = std::abs(screenMax.x - screenMin.x);
        float meshPixelHeight = std::abs(screenMax.y - screenMin.y);

        if (meshPixelWidth <= 0.0f || meshPixelHeight <= 0.0f) {
            mesh->scale = 1.0f;
            return 1;
        }

        float scaleX = rectSize.x / meshPixelWidth;
        float scaleY = rectSize.y / meshPixelHeight;

        return scaleX < scaleY ? scaleX : scaleY;
    }
}