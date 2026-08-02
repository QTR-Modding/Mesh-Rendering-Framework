#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#define ENABLE_MENU_FRAMEWORK

#ifdef ENABLE_MENU_FRAMEWORK
    #include "SKSEMenuFramework.h"
#endif

namespace MeshRenderingFrameworkAPI {

    struct BoneTransform {
        float translation[3]{};
        float rotation[4]{0.0f, 0.0f, 0.0f, 1.0f};
        float scale[3]{1.0f, 1.0f, 1.0f};
    };

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
            float bodyTintColor[3]{1.0f, 1.0f, 1.0f};
            bool useBodyTint = false;
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

        inline IMesh* __stdcall IMesh_CreateByNifPathSet(const char* const* basePaths, uint32_t basePathCount, const char* const* attachmentPaths, uint32_t attachmentPathCount, uint32_t width, uint32_t height) {
            auto function = GetFunction<decltype(&IMesh_CreateByNifPathSet)>("IMesh_CreateByNifPathSet");
            if (!function) {
                return nullptr;
            }
            return function(basePaths, basePathCount, attachmentPaths, attachmentPathCount, width, height);
        }

        inline IMesh* __stdcall IMesh_CreateByNiAVObjectList(RE::NiAVObject* const* objects, uint32_t objectCount, uint32_t width, uint32_t height) {
            auto function = GetFunction<decltype(&IMesh_CreateByNiAVObjectList)>("IMesh_CreateByNiAVObjectList");
            if (!function) {
                return nullptr;
            }
            return function(objects, objectCount, width, height);
        }

        inline bool __stdcall IMesh_SetBoneLocalPose(IMesh* mesh, const char* const* boneNames, const std::int16_t* parentIndices, const BoneTransform* transforms, uint32_t transformCount) {
            auto function = GetFunction<decltype(&IMesh_SetBoneLocalPose)>("IMesh_SetBoneLocalPose");
            return function && function(mesh, boneNames, parentIndices, transforms, transformCount);
        }

        inline bool __stdcall IMesh_PlayAnimation(IMesh* mesh, const char* animationPath, const char* skeletonPath, bool loop) {
            auto function = GetFunction<decltype(&IMesh_PlayAnimation)>("IMesh_PlayAnimation");
            return function && function(mesh, animationPath, skeletonPath, loop);
        }

        inline bool __stdcall IMesh_SetFaceMorphSource(IMesh* mesh, RE::Actor* actor) {
            auto function = GetFunction<decltype(&IMesh_SetFaceMorphSource)>("IMesh_SetFaceMorphSource");
            return function && function(mesh, actor);
        }

        inline bool __stdcall IMesh_SetMorph(IMesh* mesh, const char* triPath, const char* morphName, float value) {
            auto function = GetFunction<decltype(&IMesh_SetMorph)>("IMesh_SetMorph");
            return function && function(mesh, triPath, morphName, value);
        }

        inline bool __stdcall IMesh_ClearFaceMorphs(IMesh* mesh) {
            auto function = GetFunction<decltype(&IMesh_ClearFaceMorphs)>("IMesh_ClearFaceMorphs");
            return function && function(mesh);
        }

        inline bool __stdcall IMesh_SetTextureSet(IMesh* mesh, const char* nifPath, const char* const* texturePaths, std::uint32_t texturePathCount, bool modelSpaceNormals, bool includeBodyShape) {
            auto function = GetFunction<decltype(&IMesh_SetTextureSet)>("IMesh_SetTextureSet");
            return function && function(mesh, nifPath, texturePaths, texturePathCount, modelSpaceNormals, includeBodyShape);
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

        inline std::vector<std::string> GetNpcFaceGenPaths(RE::TESNPC* npc) {
            if (!npc) {
                return {};
            }

            RE::TESNPC* faceNpc = npc->GetRootFaceNPC();
            if (!faceNpc) {
                faceNpc = npc;
            }

            RE::TESFileArray* faceFiles = faceNpc->sourceFiles.array;
            if (!faceFiles || faceFiles->empty() || !(*faceFiles)[0]) {
                return {};
            }

            char formId[9]{};
            const int written = std::snprintf(formId, sizeof(formId), "%08X", static_cast<unsigned int>(faceNpc->GetLocalFormID()));
            if (written != 8) {
                return {};
            }

            std::vector<std::string> paths;
            paths.reserve(faceFiles->size());
            // FaceGen files live under the plugin that exported them. Prefer the
            // winning override, then fall back through its masters.
            for (std::size_t fileIndex = faceFiles->size(); fileIndex > 0; --fileIndex) {
                RE::TESFile* faceFile = (*faceFiles)[fileIndex - 1];
                if (!faceFile) {
                    continue;
                }

                const std::string_view fileName = faceFile->GetFilename();
                if (fileName.empty()) {
                    continue;
                }

                std::string path = "actors\\character\\FaceGenData\\FaceGeom\\";
                path.append(fileName);
                path.push_back('\\');
                path.append(formId);
                path.append(".nif");
                paths.push_back(std::move(path));
            }
            return paths;
        }

        inline void AppendUniqueNifPath(std::vector<std::string>& paths, const char* path) {
            if (!path || !path[0]) {
                return;
            }

            const std::string value(path);
            if (std::find(paths.begin(), paths.end(), value) == paths.end()) {
                paths.push_back(value);
            }
        }

        inline constexpr std::size_t NpcTexturePathCount = static_cast<std::size_t>(RE::BSTextureSet::Textures::kUsedTotal);

        struct NpcTextureOverride {
            std::string nifPath;
            std::array<std::string, NpcTexturePathCount> texturePaths;
            bool modelSpaceNormals = false;
            bool includeBodyShape = false;
        };

        inline void AppendNpcTextureOverride(std::vector<NpcTextureOverride>* overrides, const char* nifPath, RE::BGSTextureSet* textureSet, bool includeBodyShape) {
            if (!overrides || !nifPath || !nifPath[0] || !textureSet) {
                return;
            }

            NpcTextureOverride textureOverride;
            textureOverride.nifPath = nifPath;
            textureOverride.modelSpaceNormals = textureSet->flags.any(RE::BGSTextureSet::Flag::kHasModelSpaceNormalMap);
            textureOverride.includeBodyShape = includeBodyShape;
            for (std::size_t textureIndex = 0; textureIndex < NpcTexturePathCount; ++textureIndex) {
                const char* texturePath = textureSet->GetTexturePath(static_cast<RE::BSTextureSet::Texture>(textureIndex));
                if (texturePath) {
                    textureOverride.texturePaths[textureIndex] = texturePath;
                }
            }
            overrides->push_back(std::move(textureOverride));
        }

        inline RE::BGSTextureSet* GetNpcHeadTextureSet(RE::TESNPC* npc, RE::TESRace* race, RE::SEX sex, RE::BGSHeadPart* headPart) {
            if (!headPart) {
                return nullptr;
            }

            if (headPart->type != RE::BGSHeadPart::HeadPartType::kFace) {
                return headPart->textureSet;
            }

            RE::TESNPC* faceNpc = npc ? npc->GetRootFaceNPC() : nullptr;
            if (!faceNpc) {
                faceNpc = npc;
            }
            if (faceNpc &&
                faceNpc->GetRace() == race &&
                faceNpc->headRelatedData &&
                faceNpc->headRelatedData->faceDetails) {
                return faceNpc->headRelatedData->faceDetails;
            }

            RE::TESRace::FaceRelatedData* faceData = race ? race->faceRelatedData[sex] : nullptr;
            if (faceData && faceData->defaultFaceDetailsTextureSet) {
                return faceData->defaultFaceDetailsTextureSet;
            }
            return headPart->textureSet;
        }

        inline RE::BGSTextureSet* GetNpcSkinTextureSet(RE::TESNPC* npc, RE::TESRace* race, RE::SEX sex, std::uint32_t slots) {
            if (!race) {
                return nullptr;
            }

            const auto getArmorTextureSet = [race, sex, slots](RE::TESObjectARMO* skinArmor) -> RE::BGSTextureSet* {
                if (!skinArmor) {
                    return nullptr;
                }

                for (RE::TESObjectARMA* armorAddon : skinArmor->armorAddons) {
                    if (!armorAddon || !armorAddon->IsValidRace(race)) {
                        continue;
                    }

                    const std::uint32_t addonSlots = static_cast<std::uint32_t>(*armorAddon->bipedModelData.bipedObjectSlots);
                    if ((addonSlots & slots) != 0 && armorAddon->skinTextures[sex]) {
                        return armorAddon->skinTextures[sex];
                    }
                }
                return nullptr;
            };

            RE::BGSTextureSet* textureSet = npc ? getArmorTextureSet(npc->skin) : nullptr;
            if (!textureSet && race) {
                textureSet = getArmorTextureSet(race->skin);
            }
            if (!textureSet && npc) {
                textureSet = getArmorTextureSet(npc->farSkin);
            }
            return textureSet;
        }

        inline std::uint32_t AppendArmorModelPaths(RE::TESObjectARMO* armor, RE::TESRace* race, RE::SEX sex, std::vector<std::string>& paths, std::uint32_t excludedSlots = 0, bool excludeOnAnyOverlap = false,
                                                   std::vector<NpcTextureOverride>* textureOverrides = nullptr, bool textureSetIncludesBodyShape = false, RE::TESNPC* skinNpc = nullptr) {
            if (!armor || !race) {
                return 0;
            }

            std::uint32_t addedSlots = 0;
            for (RE::TESObjectARMA* armorAddon : armor->armorAddons) {
                if (!armorAddon || !armorAddon->IsValidRace(race)) {
                    continue;
                }
                const std::uint32_t addonSlots = static_cast<std::uint32_t>(*armorAddon->bipedModelData.bipedObjectSlots);
                const std::uint32_t overlappingSlots = addonSlots & excludedSlots;
                if ((excludeOnAnyOverlap && overlappingSlots != 0) || (!excludeOnAnyOverlap && (addonSlots & ~excludedSlots) == 0)) {
                    continue;
                }

                const char* modelPath = armorAddon->bipedModels[sex].GetModel();
                if (modelPath && modelPath[0]) {
                    AppendUniqueNifPath(paths, modelPath);
                    RE::BGSTextureSet* textureSet = armorAddon->skinTextures[sex];
                    if (skinNpc) {
                        // Exposed skin embedded in an outfit uses the actor's skin armor,
                        // including its race-specific body, hand, or foot texture set.
                        RE::BGSTextureSet* npcSkinTextureSet = GetNpcSkinTextureSet(skinNpc, race, sex, addonSlots);
                        if (npcSkinTextureSet) {
                            textureSet = npcSkinTextureSet;
                        }
                    }
                    AppendNpcTextureOverride(textureOverrides, modelPath, textureSet, textureSetIncludesBodyShape);
                    addedSlots |= addonSlots;
                }
            }
            return addedSlots;
        }

        inline bool NifResourceExists(const std::string& path) {
            if (path.empty()) {
                return false;
            }

            std::string resourcePath = path;
            std::replace(resourcePath.begin(), resourcePath.end(), '/', '\\');
            std::string lowerPath = resourcePath;
            std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            if (!lowerPath.starts_with("meshes\\")) {
                resourcePath.insert(0, "meshes\\");
            }

            RE::BSResourceNiBinaryStream stream(resourcePath);
            return stream.good() && stream.stream && stream.stream->totalSize > 0;
        }

        inline void AttachNpcFaceMorphSource(IMesh* mesh, RE::TESNPC* npc) {
            if (!mesh || !npc) {
                return;
            }

            RE::Actor* actor = npc->GetUniqueActor();
            if (actor) {
                IMesh_SetFaceMorphSource(mesh, actor);
            }
        }

        inline IMesh* CreateWholeNpc(RE::TESNPC* npc, uint32_t width, uint32_t height, bool includeDefaultOutfit = true, RE::Actor* actor = nullptr) {
            if (!npc) {
                return nullptr;
            }

            RE::TESRace* race = actor ? actor->GetRace() : npc->GetRace();
            if (!race) {
                return nullptr;
            }
            const RE::SEX sex = npc->GetSex();
            std::vector<std::string> componentPaths;
            std::vector<NpcTextureOverride> textureOverrides;

            std::uint32_t outfitSlots = 0;
            if (actor) {
                std::vector<RE::TESObjectARMO*> wornArmors;
                wornArmors.reserve(8);
                for (std::uint32_t slotIndex = 0; slotIndex < 32; ++slotIndex) {
                    const auto slot = static_cast<RE::BGSBipedObjectForm::BipedObjectSlot>(std::uint32_t{1} << slotIndex);
                    RE::TESObjectARMO* armor = actor->GetWornArmor(slot);
                    if (!armor || std::find(wornArmors.begin(), wornArmors.end(), armor) != wornArmors.end()) {
                        continue;
                    }

                    wornArmors.push_back(armor);
                    outfitSlots |= AppendArmorModelPaths(armor, race, sex, componentPaths, 0, false, &textureOverrides, false, npc);
                }
            } else if (includeDefaultOutfit && npc->defaultOutfit) {
                for (RE::TESForm* outfitItem : npc->defaultOutfit->outfitItems) {
                    RE::TESObjectARMO* armor = outfitItem ? outfitItem->As<RE::TESObjectARMO>() : nullptr;
                    outfitSlots |= AppendArmorModelPaths(armor, race, sex, componentPaths, 0, false, &textureOverrides, false, npc);
                }
            }

            const std::size_t pathCountBeforeSkin = componentPaths.size();
            AppendArmorModelPaths(npc->skin, race, sex, componentPaths, outfitSlots, true, &textureOverrides, true);
            if (componentPaths.size() == pathCountBeforeSkin) {
                AppendArmorModelPaths(race->skin, race, sex, componentPaths, outfitSlots, true, &textureOverrides, true);
            }
            if (componentPaths.size() == pathCountBeforeSkin) {
                AppendArmorModelPaths(npc->farSkin, race, sex, componentPaths, outfitSlots, true, &textureOverrides, true);
            }

            bool addedFaceGen = false;
            if (!actor) {
                const std::vector<std::string> faceGenPaths = GetNpcFaceGenPaths(npc);
                for (const std::string& faceGenPath : faceGenPaths) {
                    if (NifResourceExists(faceGenPath)) {
                        AppendUniqueNifPath(componentPaths, faceGenPath.c_str());
                        addedFaceGen = true;
                        break;
                    }
                }
            }

            if (!addedFaceGen && npc->headParts && npc->numHeadParts > 0) {
                for (std::int8_t headPartIndex = 0; headPartIndex < npc->numHeadParts; ++headPartIndex) {
                    RE::BGSHeadPart* headPart = npc->headParts[headPartIndex];
                    if (headPart) {
                        const char* headPartPath = headPart->GetModel();
                        AppendUniqueNifPath(componentPaths, headPartPath);
                        AppendNpcTextureOverride(&textureOverrides, headPartPath, GetNpcHeadTextureSet(npc, race, sex, headPart), false);
                    }
                }
            }

            if (componentPaths.empty()) {
                return nullptr;
            }

            const char* basePath = componentPaths.front().c_str();
            std::vector<const char*> attachmentPaths;
            attachmentPaths.reserve(componentPaths.size() - 1);
            for (std::size_t pathIndex = 1; pathIndex < componentPaths.size(); ++pathIndex) {
                attachmentPaths.push_back(componentPaths[pathIndex].c_str());
            }

            IMesh* mesh = IMesh_CreateByNifPathSet(&basePath, 1, attachmentPaths.data(), static_cast<uint32_t>(attachmentPaths.size()), width, height);
            if (!mesh) {
                return nullptr;
            }

            for (const NpcTextureOverride& textureOverride : textureOverrides) {
                std::array<const char*, NpcTexturePathCount> texturePaths{};
                for (std::size_t textureIndex = 0; textureIndex < texturePaths.size(); ++textureIndex) {
                    texturePaths[textureIndex] = textureOverride.texturePaths[textureIndex].c_str();
                }
                IMesh_SetTextureSet(mesh, textureOverride.nifPath.c_str(), texturePaths.data(), static_cast<std::uint32_t>(texturePaths.size()), textureOverride.modelSpaceNormals, textureOverride.includeBodyShape);
            }

            RE::TESNPC* tintNpc = npc;
            if (tintNpc->bodyTintColor.red == 0 && tintNpc->bodyTintColor.green == 0 && tintNpc->bodyTintColor.blue == 0) {
                RE::TESNPC* rootFaceNpc = npc->GetRootFaceNPC();
                if (rootFaceNpc) {
                    tintNpc = rootFaceNpc;
                }
            }

            const RE::Color& bodyTint = tintNpc->bodyTintColor;
            if (bodyTint.red != 0 || bodyTint.green != 0 || bodyTint.blue != 0) {
                constexpr float byteToFloat = 1.0f / 255.0f;
                mesh->bodyTintColor[0] = static_cast<float>(bodyTint.red) * byteToFloat;
                mesh->bodyTintColor[1] = static_cast<float>(bodyTint.green) * byteToFloat;
                mesh->bodyTintColor[2] = static_cast<float>(bodyTint.blue) * byteToFloat;
                mesh->useBodyTint = true;
                mesh->mustUpdate = true;
            }
            if (actor) {
                IMesh_SetFaceMorphSource(mesh, actor);
            } else {
                AttachNpcFaceMorphSource(mesh, npc);
            }
            return mesh;
        }

        inline IMesh* CreateFromBaseObject(RE::TESBoundObject* base, uint32_t width, uint32_t height) {
            if (!base) {
                return nullptr;
            }
            if (auto weapon = base->As<RE::TESObjectWEAP>()) {
                if (auto first = weapon->firstPersonModelObject) {
                    return IMesh_CreateByNifPath(first->GetModel(), width, height);
                }
            }

            if (auto npc = base->As<RE::TESNPC>()) {
                auto sex = npc->GetSex();
                auto race = npc->GetRace();

                const std::vector<std::string> faceGenPaths = GetNpcFaceGenPaths(npc);
                std::vector<const char*> basePaths;
                std::vector<const char*> attachmentPaths;
                basePaths.reserve(faceGenPaths.size() + 1);
                for (const std::string& faceGenPath : faceGenPaths) {
                    basePaths.push_back(faceGenPath.c_str());
                }

                if (npc->headParts && npc->numHeadParts > 0) {
                    for (std::int8_t i = 0; i < npc->numHeadParts; ++i) {
                        RE::BGSHeadPart* headPart = npc->headParts[i];
                        if (!headPart) {
                            continue;
                        }

                        const char* path = headPart->GetModel();
                        if (!path || !path[0]) {
                            continue;
                        }

                        if (headPart->type == RE::BGSHeadPart::HeadPartType::kFace) {
                            basePaths.push_back(path);
                        } else {
                            attachmentPaths.push_back(path);
                        }
                    }
                }

                if (!basePaths.empty()) {
                    if (IMesh* mesh = IMesh_CreateByNifPathSet(basePaths.data(), static_cast<uint32_t>(basePaths.size()), attachmentPaths.data(), static_cast<uint32_t>(attachmentPaths.size()), width, height)) {
                        AttachNpcFaceMorphSource(mesh, npc);
                        return mesh;
                    }
                }

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

        inline IMesh* CreateFromNiAVObjectList(RE::NiAVObject* const* objects, uint32_t objectCount, uint32_t width, uint32_t height) {
            // Retained for ABI compatibility. The nifly renderer requires a NIF
            // resource path, so live scene-object lists are not renderable.
            if (!objects || objectCount == 0) {
                return nullptr;
            }

            return IMesh_CreateByNiAVObjectList(objects, objectCount, width, height);
        }

        inline IMesh* CreateFromActor(RE::Actor* actor, uint32_t width, uint32_t height) {
            if (!actor) {
                return nullptr;
            }

            return CreateWholeNpc(actor->GetActorBase(), width, height, false, actor);
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
            mesh->alwaysUpdate = value;
        }
        bool SetBoneLocalPose(const char* const* boneNames, const std::int16_t* parentIndices, const BoneTransform* transforms, uint32_t transformCount) {
            if (!mesh) {
                return false;
            }
            return Internal::IMesh_SetBoneLocalPose(mesh, boneNames, parentIndices, transforms, transformCount);
        }
        bool PlayAnimation(const char* animationPath, bool loop = true, const char* skeletonPath = "meshes\\actors\\character\\character assets\\skeleton.hkx") {
            if (!mesh) {
                return false;
            }
            return Internal::IMesh_PlayAnimation(mesh, animationPath, skeletonPath, loop);
        }
        bool SetFaceMorphSource(RE::Actor* actor) {
            if (!mesh || !actor) {
                return false;
            }
            return Internal::IMesh_SetFaceMorphSource(mesh, actor);
        }
        bool SetMorph(const char* triPath, const char* morphName, float value = 1.0f) {
            if (!mesh || !triPath || !morphName) {
                return false;
            }
            return Internal::IMesh_SetMorph(mesh, triPath, morphName, value);
        }
        bool ClearFaceMorphs() { return mesh && Internal::IMesh_ClearFaceMorphs(mesh); }
        bool SetExpression(RE::BSFaceGenKeyframeMultiple::Expression expression, float value = 1.0f) {
            if (!mesh || !base) {
                return false;
            }

            RE::TESNPC* npc = base->As<RE::TESNPC>();
            if (!npc) {
                return false;
            }

            const bool cleared = ClearFaceMorphs();
            if (expression == RE::BSFaceGenKeyframeMultiple::MoodNeutral) {
                return cleared;
            }

            const char* expressionName = RE::BSFaceGenKeyframeMultiple::GetExpressionName(static_cast<std::uint32_t>(expression));
            if (!expressionName || !expressionName[0]) {
                return false;
            }

            RE::TESNPC* faceNpc = npc->GetRootFaceNPC();
            if (!faceNpc) {
                faceNpc = npc;
            }

            std::vector<RE::BGSHeadPart*> pending;
            std::vector<RE::BGSHeadPart*> processed;
            pending.reserve(faceNpc->numHeadParts + npc->numHeadParts);
            RE::TESNPC* faceSources[2]{faceNpc, npc};
            for (RE::TESNPC* faceSource : faceSources) {
                if (!faceSource || !faceSource->headParts || faceSource->numHeadParts <= 0) {
                    continue;
                }
                for (std::int8_t headPartIndex = 0; headPartIndex < faceSource->numHeadParts; ++headPartIndex) {
                    if (faceSource->headParts[headPartIndex]) {
                        pending.push_back(faceSource->headParts[headPartIndex]);
                    }
                }
            }

            bool applied = false;
            while (!pending.empty()) {
                RE::BGSHeadPart* headPart = pending.back();
                pending.pop_back();
                if (!headPart || std::find(processed.begin(), processed.end(), headPart) != processed.end()) {
                    continue;
                }
                processed.push_back(headPart);

                const char* triPath = headPart->morphs[RE::BGSHeadPart::MorphIndices::kDefaultMorph].GetModel();
                if (triPath && triPath[0]) {
                    applied = SetMorph(triPath, expressionName, value) || applied;
                }

                for (RE::BGSHeadPart* extraPart : headPart->extraParts) {
                    if (extraPart) {
                        pending.push_back(extraPart);
                    }
                }
            }
            return applied;
        }
        ~Mesh() {
            if (mesh) {
                Internal::IMesh_Delete(mesh);
            }
        }

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
        // The meshes\ prefix is optional.
        Mesh(const char* path, uint32_t width, uint32_t height) {
            base = nullptr;
            mesh = Internal::IMesh_CreateByNifPath(path, width, height);
        }
        Mesh(RE::NiAVObject* const* objects, uint32_t objectCount, uint32_t width, uint32_t height) {
            base = nullptr;
            mesh = Internal::CreateFromNiAVObjectList(objects, objectCount, width, height);
        }
        Mesh(const std::vector<RE::NiAVObject*>& objects, uint32_t width, uint32_t height) {
            base = nullptr;
            mesh = Internal::CreateFromNiAVObjectList(objects.data(), static_cast<uint32_t>(objects.size()), width, height);
        }
        Mesh(RE::Actor* actor, uint32_t width, uint32_t height) {
            base = actor ? actor->GetActorBase() : nullptr;
            mesh = Internal::CreateFromActor(actor, width, height);
        }

    protected:
        Mesh(Internal::IMesh* createdMesh, RE::TESBoundObject* sourceBase) : mesh(createdMesh), base(sourceBase) {}
    };

    class WholeNpcMesh : public Mesh {
    public:
        WholeNpcMesh(RE::TESNPC* npc, uint32_t width, uint32_t height, bool includeDefaultOutfit = true) : Mesh(Internal::CreateWholeNpc(npc, width, height, includeDefaultOutfit), npc) {}

        WholeNpcMesh(RE::FormID id, uint32_t width, uint32_t height, bool includeDefaultOutfit = true) : WholeNpcMesh(RE::TESForm::LookupByID<RE::TESNPC>(id), width, height, includeDefaultOutfit) {}
    };

#ifdef ENABLE_MENU_FRAMEWORK
    class OrbitMesh : public Mesh {
        RE::NiMatrix3 orientation;
        bool orientationChanged = true;
        uint32_t renderWidth;
        uint32_t renderHeight;

    protected:
        OrbitMesh(Internal::IMesh* createdMesh, RE::TESBoundObject* sourceBase, uint32_t width, uint32_t height) : Mesh(createdMesh, sourceBase), renderWidth(width), renderHeight(height) { ScaleUp(0.8f); }

    public:
        void SetOrbitOrientation(RE::NiMatrix3 orientation) {
            if (!mesh) {
                return;
            }

            this->orientation = orientation;
            orientationChanged = true;
        }

        OrbitMesh(RE::TESBoundObject* base, uint32_t width, uint32_t height) : Mesh(base, width, height), renderWidth(width), renderHeight(height) { ScaleUp(0.8f); }

        OrbitMesh(RE::FormID id, uint32_t width, uint32_t height) : Mesh(id, width, height), renderWidth(width), renderHeight(height) { ScaleUp(0.8f); }

        OrbitMesh(const char* path, uint32_t width, uint32_t height) : Mesh(path, width, height), renderWidth(width), renderHeight(height) { ScaleUp(0.8f); }

        OrbitMesh(RE::NiAVObject* const* objects, uint32_t objectCount, uint32_t width, uint32_t height) : Mesh(objects, objectCount, width, height), renderWidth(width), renderHeight(height) { ScaleUp(0.8f); }

        OrbitMesh(const std::vector<RE::NiAVObject*>& objects, uint32_t width, uint32_t height) : Mesh(objects, width, height), renderWidth(width), renderHeight(height) { ScaleUp(0.8f); }

        OrbitMesh(RE::Actor* actor, uint32_t width, uint32_t height) : Mesh(actor, width, height), renderWidth(width), renderHeight(height) { ScaleUp(0.8f); }

        void Render(const char* name) {
            const ImGuiMCP::ImVec2 imageSize{static_cast<float>(renderWidth), static_cast<float>(renderHeight)};

            ImGuiMCP::InvisibleButton(name, imageSize);
            const auto imageMin = ImGuiMCP::GetItemRectMin();
            const auto imageMax = ImGuiMCP::GetItemRectMax();
            ImGuiMCP::ImDrawList* drawList = ImGuiMCP::GetWindowDrawList();
            ID3D11ShaderResourceView* resourceView = GetResourceView();

            if (resourceView) {
                ImGuiMCP::ImDrawListManager::AddImage(drawList, (ImGuiMCP::ImTextureID)resourceView, imageMin, imageMax, {0, 0}, {1, 1}, IM_COL32_WHITE);

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
            } else {
                ImGuiMCP::ImDrawListManager::AddRectFilled(drawList, imageMin, imageMax, IM_COL32(32, 32, 32, 255), 0.0f, 0);
                ImGuiMCP::ImDrawListManager::AddRect(drawList, imageMin, imageMax, IM_COL32(96, 96, 96, 255), 0.0f, 0, 1.0f);
                ImGuiMCP::ImDrawListManager::AddText(drawList, {imageMin.x + 8.0f, imageMin.y + 8.0f}, IM_COL32(192, 192, 192, 255), "Mesh unavailable");
            }
        }
    };

    class WholeNpcOrbitMesh : public OrbitMesh {
    public:
        WholeNpcOrbitMesh(RE::TESNPC* npc, uint32_t width, uint32_t height, bool includeDefaultOutfit = true) : OrbitMesh(Internal::CreateWholeNpc(npc, width, height, includeDefaultOutfit), npc, width, height) {}

        WholeNpcOrbitMesh(RE::FormID id, uint32_t width, uint32_t height, bool includeDefaultOutfit = true) : WholeNpcOrbitMesh(RE::TESForm::LookupByID<RE::TESNPC>(id), width, height, includeDefaultOutfit) {}
    };
#endif
}
