#include "UI.h"
#include "RenderManager.h"
#include "MeshRenderingFrameworkAPI.h"
void UI::Register() {
    if (!SKSEMenuFramework::IsInstalled()) {
        return;
    }
    SKSEMenuFramework::SetSection(MOD_NAME);
    SKSEMenuFramework::AddSectionItem("Resource Inspector", ResourceInspector::Render);
}

void __stdcall UI::ResourceInspector::Render() {
    ImGuiMCP::Text("Cached Render Target");
    for (auto& [key, value] : RenderManager::renderTarget) {
        ImGuiMCP::Text(key.c_str());
    }
    ImGuiMCP::Text("Cached Meshes");
    for (auto& [key, value] : RenderManager::meshes) {
        if (value->nif) {
            ImGuiMCP::Text(std::format("{}", value->nif->nifPath).c_str());
        }
    }
}
