#pragma once

#define ENABLE_MENU_FRAMEWORK

#ifdef ENABLE_MENU_FRAMEWORK
    #include "SKSEMenuFramework.h"
#endif

namespace MeshRenderingFrameworkAPI {

    namespace Internal {
        class IMesh {
        public:
            uint64_t id;
            RE::NiMatrix3 rotation;
            RE::NiPoint3 position;
            RE::NiPoint3 boundMin;
            RE::NiPoint3 boundMax;
            float scale = 1;
            uint32_t width;
            uint32_t height;
            ID3D11Texture2D* texture = nullptr;
            ID3D11ShaderResourceView* SRV = nullptr;
            bool saveNextFrame = false;
            bool deleteAfterSave = false;
            const char* savePath = nullptr;
            bool mustUpdate = true;
            bool alwaysUpdate = false;
        };

        template <class T>
        T GetFunction(LPCSTR name) {
            static auto meshRenderer = GetModuleHandle(L"MeshRenderingFramework");
            if (!meshRenderer) {
                return nullptr;
            }
            return reinterpret_cast<T>(GetProcAddress(meshRenderer, name));
        }

        inline IMesh* __stdcall IMesh_CreateByNifPath(const char* nifPath, uint32_t width, uint32_t height) {
            auto function = GetFunction<decltype(&IMesh_CreateByNifPath)>("IMesh_CreateByNifPath");
            if (!function) {
                return nullptr;
            }
            return function(nifPath, width, height);
        }

        inline void __stdcall IMesh_Delete(IMesh* mesh) {
            auto function = GetFunction<decltype(&IMesh_Delete)>("IMesh_Delete");
            if (!function) {
                return;
            }
            return function(mesh);
        }

        inline IMesh* __stdcall IMesh_Save(IMesh* mesh, const char* filePath) {
            auto function = GetFunction<decltype(&IMesh_Save)>("IMesh_Save");
            if (!function) {
                return nullptr;
            }
            return function(mesh, filePath);
        }

        inline IMesh* CreateFromBaseObject(RE::TESBoundObject* base, uint32_t width, uint32_t  height) {
            if (auto weapon = base->As<RE::TESObjectWEAP>()) {
                if (auto first = weapon->firstPersonModelObject) {
                    return IMesh_CreateByNifPath(first->GetModel(), width, height);
                }
            }

            if (auto npc = base->As<RE::TESNPC>()) {
                auto sex = npc->GetSex();
                auto race = npc->GetRace();

                auto createFromArmor = [&](RE::TESObjectARMO* armor) -> IMesh* {
                    if (!armor || !race) {
                        return nullptr;
                    }

                    if (auto arma = armor->GetArmorAddon(race)) {
                        auto path = arma->bipedModels[sex].GetModel();
                        if (path && path[0]) {
                            return IMesh_CreateByNifPath(path, width, height);
                        }
                    }

                    return nullptr;
                };

                if (auto mesh = createFromArmor(npc->skin)) {
                    return mesh;
                }

                if (race) {
                    if (auto mesh = createFromArmor(race->skin)) {
                        return mesh;
                    }
                }

                if (auto mesh = createFromArmor(npc->farSkin)) {
                    return mesh;
                }

                for (std::int8_t i = 0; i < npc->numHeadParts; ++i) {
                    if (auto headPart = npc->headParts[i]) {
                        auto path = headPart->GetModel();
                        if (path && path[0]) {
                            return IMesh_CreateByNifPath(path, width, height);
                        }
                    }
                }
            }

            auto swap = base->As<RE::TESModel>();

            if (swap) {
                return IMesh_CreateByNifPath(swap->GetModel(), width, height);
            }
            if (auto biped = base->As<RE::TESBipedModelForm>()) {
                auto player = RE::PlayerCharacter::GetSingleton();
                auto playerBase = player->GetActorBase();
                auto sex = playerBase->GetSex();
                auto& worldModel = biped->worldModels[sex];
                return IMesh_CreateByNifPath(worldModel.GetModel(), width, height);
            }

            if (auto spell = base->As<RE::SpellItem>()) {
                if (auto obj = spell->GetMenuDisplayObject()) {
                    if (auto model = obj->As<RE::TESModel>()) {
                        return IMesh_CreateByNifPath(model->GetModel(), width, height);
                    }
                }
            }

            return nullptr;
        }

    }

    class Mesh {
    protected:
        Internal::IMesh* mesh;
        RE::TESBoundObject* base;

    public:
        static void Render(const char* filePath, RE::TESBoundObject* base, uint32_t width, uint32_t height) {
            std::filesystem::path path(filePath);
            if (std::filesystem::exists(path)) {
                return;
            }
            auto mesh = new Mesh(base, width, height);
            mesh->Save(filePath);
            if (mesh->mesh) {
                mesh->mesh->deleteAfterSave = true;
            }
        }
        static void Render(const char* filePath, RE::FormID id, uint32_t width, uint32_t height) {
            std::filesystem::path path(filePath);
            if (std::filesystem::exists(path)) {
                return;
            }
            auto mesh = new Mesh(id, width, height);
            mesh->Save(filePath);
            if (mesh->mesh) {
                mesh->mesh->deleteAfterSave = true;
            }
        }
        static void Render(const char* filePath, const char* nifPath, uint32_t width, uint32_t height) {
            std::filesystem::path path(filePath);
            if (std::filesystem::exists(path)) {
                return;
            }
            auto mesh = new Mesh(nifPath, width, height);
            mesh->Save(filePath);
            if (mesh->mesh) {
                mesh->mesh->deleteAfterSave = true;
            }
        }
        void Save(const char* filePath) {
            if (!mesh) {
                return;
            }
            if (mesh->SRV) {
                Internal::IMesh_Save(mesh, filePath);
            } else {
                mesh->savePath = strdup(filePath);
                mesh->saveNextFrame = true;
            }
        }
        ID3D11ShaderResourceView* GetResourceView() {
            if (!mesh) {
                return nullptr;
            }
            return mesh->SRV;
        }
        RE::TESBoundObject* GetBase() { return base; }
        void SetRotation(RE::NiMatrix3 rotation) {
            if (!mesh) {
                return;
            }
            mesh->rotation = rotation;
            mesh->mustUpdate = true;
        }
        void SetPosition(RE::NiPoint3 position) {
            if (!mesh) {
                return;
            }
            mesh->position = position;
            mesh->mustUpdate = true;
        }
        void ScaleUp(float scale) {
            if (!mesh) {
                return;
            }
            mesh->scale *= scale;
            mesh->mustUpdate = true;
        }
        void SetAlwaysUpdate(bool value) {
            if (!mesh) {
                return;
            }
            mesh->alwaysUpdate = true;
        }
        ~Mesh() { Internal::IMesh_Delete(mesh); }

        Mesh(RE::TESBoundObject* base, uint32_t width, uint32_t height) {
            this->base = base;
            mesh = Internal::CreateFromBaseObject(base, width, height);
        }

        Mesh(RE::FormID id, uint32_t width, uint32_t height) {
            base = RE::TESForm::LookupByID<RE::TESBoundObject>(id);
            if (!base) {
                mesh = nullptr;
                return;
            }
            mesh = Internal::CreateFromBaseObject(base, width, height);
        }
        Mesh(const char* path, uint32_t width, uint32_t height) {
            base = nullptr;
            mesh = Internal::IMesh_CreateByNifPath(path, width, height);
        }
    };

#ifdef ENABLE_MENU_FRAMEWORK
    class OrbitMesh : public Mesh {
        RE::NiMatrix3 orientation;
        bool orientationChanged = true;

    public:
        void SetOrbitOrientation(RE::NiMatrix3 orientation) {
            if (!mesh) {
                return;
            }

            this->orientation = orientation;
            orientationChanged = true;
        }

        OrbitMesh(RE::TESBoundObject* base, uint32_t width, uint32_t height) : Mesh(base, width, height) { ScaleUp(0.8f); }

        OrbitMesh(RE::FormID id, uint32_t width, uint32_t height) : Mesh(id, width, height) { ScaleUp(0.8f); }

        OrbitMesh(const char* path, uint32_t width, uint32_t height) : Mesh(path, width, height) { ScaleUp(0.8f); }

        void Render(const char* name) {
            if (GetResourceView()) {
                const ImGuiMCP::ImVec2 imageSize{mesh->width, mesh->height};

                ImGuiMCP::InvisibleButton(name, imageSize);

                const auto imageMin = ImGuiMCP::GetItemRectMin();
                const auto imageMax = ImGuiMCP::GetItemRectMax();

                ImGuiMCP::ImDrawListManager::AddImage(ImGuiMCP::GetWindowDrawList(), (ImGuiMCP::ImTextureID)GetResourceView(), imageMin, imageMax, {0, 0}, {1, 1}, IM_COL32_WHITE);

                if (ImGuiMCP::IsItemActive() && ImGuiMCP::IsMouseDown(ImGuiMCP::ImGuiMouseButton_Left)) {
                    const auto delta = ImGuiMCP::GetIO()->MouseDelta;

                    if (delta.x != 0.0f || delta.y != 0.0f) {
                        const auto camera = RE::UI3DSceneManager::GetSingleton()->camera;

                        RE::NiMatrix3 yaw;
                        yaw.SetEulerAnglesXYZ(0.0f, -delta.x * 0.01f, 0.0f);

                        RE::NiMatrix3 pitch;
                        pitch.SetEulerAnglesXYZ(0.0f, 0.0f, -delta.y * 0.01f);

                        const auto cameraRotation = camera->world.rotate;
                        const auto cameraDelta = cameraRotation * pitch * yaw * cameraRotation.Transpose();

                        orientation = cameraDelta * orientation;
                        orientationChanged = true;
                    }
                }

                if (orientationChanged) {
                    SetRotation(orientation);
                    orientationChanged = false;
                }
            }
        }
    };
#endif
}
