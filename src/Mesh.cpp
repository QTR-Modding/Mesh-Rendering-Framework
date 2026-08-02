#include "Mesh.h"

#include "GameAnimation.h"
#include "MeshMath.h"

#include "RE/N/NiMath.h"

#include <DirectXTex.h>
#include <ExtraData.hpp>
#include <Geometry.hpp>
#include <NifFile.hpp>
#include <Shaders.hpp>
#include <d3d11.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace
{
    struct LiveFaceShape
    {
        std::string name;
        std::vector<RE::NiPoint3> positions;
        bool used = false;
    };

    struct TriMorph
    {
        std::vector<RE::NiPoint3> basePositions;
        std::vector<RE::NiPoint3> deltas;
    };

    template <class T>
    bool ReadBinaryValue(
        const std::vector<std::uint8_t>& bytes,
        std::size_t& offset,
        T& value)
    {
        if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
            return false;
        }
        std::memcpy(&value, bytes.data() + offset, sizeof(T));
        offset += sizeof(T);
        return true;
    }

    bool SkipBinaryValues(
        const std::vector<std::uint8_t>& bytes,
        std::size_t& offset,
        std::size_t count,
        std::size_t valueSize)
    {
        if (valueSize > 0 && count > std::numeric_limits<std::size_t>::max() / valueSize) {
            return false;
        }
        const std::size_t byteCount = count * valueSize;
        if (offset > bytes.size() || byteCount > bytes.size() - offset) {
            return false;
        }
        offset += byteCount;
        return true;
    }

    template <class T>
    void ReleaseResource(T*& resource)
    {
        if (resource) {
            resource->Release();
            resource = nullptr;
        }
    }

    bool ReadGameResource(const std::string& path, std::vector<std::uint8_t>& bytes)
    {
        const std::filesystem::path loosePath(path);
        if (std::filesystem::exists(loosePath)) {
            std::ifstream file(loosePath, std::ios::binary | std::ios::ate);
            if (!file) {
                return false;
            }

            const std::streamsize size = file.tellg();
            if (size <= 0) {
                return false;
            }

            bytes.resize(static_cast<std::size_t>(size));
            file.seekg(0, std::ios::beg);
            return file.read(reinterpret_cast<char*>(bytes.data()), size).good();
        }

        RE::BSResourceNiBinaryStream stream(path);
        if (!stream.good() || !stream.stream || stream.stream->totalSize == 0) {
            return false;
        }

        bytes.resize(stream.stream->totalSize);
        return stream.read(reinterpret_cast<char*>(bytes.data()), stream.stream->totalSize);
    }

    std::string Lowercase(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    void CollectLiveFaceShapes(RE::NiAVObject* object, std::vector<LiveFaceShape>& shapes)
    {
        if (!object) {
            return;
        }

        RE::BSDynamicTriShape* dynamicShape = object->AsDynamicTriShape();
        if (dynamicShape) {
            RE::BSDynamicTriShape::DYNAMIC_TRISHAPE_RUNTIME_DATA& dynamicData =
                dynamicShape->GetDynamicTrishapeRuntimeData();
            const std::uint16_t vertexCount =
                dynamicShape->GetTrishapeRuntimeData().vertexCount;
            const std::size_t requiredSize =
                static_cast<std::size_t>(vertexCount) * sizeof(float) * 4;
            if (dynamicData.dynamicData && vertexCount > 0 && dynamicData.dataSize >= requiredSize) {
                LiveFaceShape shape;
                shape.name = Lowercase(dynamicShape->name.c_str());
                shape.positions.resize(vertexCount);

                RE::BSSpinLockGuard lock(dynamicData.lock);
                const float* source = static_cast<const float*>(dynamicData.dynamicData);
                for (std::size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
                    shape.positions[vertexIndex] = {
                        source[vertexIndex * 4],
                        source[vertexIndex * 4 + 1],
                        source[vertexIndex * 4 + 2]
                    };
                }
                shapes.push_back(std::move(shape));
            }
        }

        RE::NiNode* node = object->AsNode();
        if (!node) {
            return;
        }
        for (const RE::NiPointer<RE::NiAVObject>& child : node->GetChildren()) {
            CollectLiveFaceShapes(child.get(), shapes);
        }
    }

    bool GameResourceExists(const std::string& path)
    {
        if (path.empty()) {
            return false;
        }

        if (std::filesystem::exists(std::filesystem::path(path))) {
            return true;
        }

        RE::BSResourceNiBinaryStream stream(path);
        return stream.good() && stream.stream && stream.stream->totalSize > 0;
    }

    std::string NormalizeResourcePath(std::string path, const std::string& rootDirectory)
    {
        if (path.empty() || std::filesystem::path(path).is_absolute()) {
            return path;
        }

        std::replace(path.begin(), path.end(), '/', '\\');
        while (path.starts_with(".\\")) {
            path.erase(0, 2);
        }
        while (!path.empty() && path.front() == '\\') {
            path.erase(path.begin());
        }

        std::string lowerPath = path;
        std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });

        if (lowerPath.starts_with("data\\")) {
            path.erase(0, 5);
            lowerPath.erase(0, 5);
        }
        const std::string lowerRoot = rootDirectory + "\\";
        if (!lowerPath.starts_with(lowerRoot)) {
            path.insert(0, rootDirectory + "\\");
        }
        return path;
    }

    std::string NormalizeNifPath(const std::string& path)
    {
        return NormalizeResourcePath(path, "meshes");
    }

    bool ReadTriMorph(
        const std::string& triPath,
        const std::string& morphName,
        TriMorph& morph)
    {
        const std::string resolvedPath = NormalizeNifPath(triPath);
        std::vector<std::uint8_t> bytes;
        if (!ReadGameResource(resolvedPath, bytes) || bytes.size() < 64) {
            logger::warn("Could not read TRI resource {} (resolved as {})", triPath, resolvedPath);
            return false;
        }

        constexpr std::string_view signature = "FRTRI003";
        if (std::memcmp(bytes.data(), signature.data(), signature.size()) != 0) {
            logger::warn("Unsupported TRI signature in {}", resolvedPath);
            return false;
        }

        std::size_t offset = signature.size();
        std::array<std::uint32_t, 10> header{};
        for (std::uint32_t& value : header) {
            if (!ReadBinaryValue(bytes, offset, value)) {
                return false;
            }
        }
        if (!SkipBinaryValues(bytes, offset, 16, sizeof(std::uint8_t))) {
            return false;
        }

        const std::uint32_t vertexCount = header[0];
        const std::uint32_t faceCount = header[1];
        const std::uint32_t quadCount = header[2];
        const std::uint32_t labelledVertexCount = header[3];
        const std::uint32_t labelledSurfacePointCount = header[4];
        const std::uint32_t uvCount = header[5];
        const std::uint32_t morphCount = header[7];
        const std::uint32_t additionalVertexCount = header[9];
        if (vertexCount == 0 ||
            quadCount != 0 ||
            labelledVertexCount != 0 ||
            labelledSurfacePointCount != 0) {
            logger::warn("Unsupported TRI layout in {}", resolvedPath);
            return false;
        }

        morph.basePositions.resize(vertexCount);
        for (RE::NiPoint3& position : morph.basePositions) {
            if (!ReadBinaryValue(bytes, offset, position.x) ||
                !ReadBinaryValue(bytes, offset, position.y) ||
                !ReadBinaryValue(bytes, offset, position.z)) {
                return false;
            }
        }

        if (!SkipBinaryValues(bytes, offset, additionalVertexCount, sizeof(float) * 3) ||
            !SkipBinaryValues(bytes, offset, faceCount, sizeof(std::uint32_t) * 3) ||
            !SkipBinaryValues(bytes, offset, uvCount, sizeof(float) * 2) ||
            !SkipBinaryValues(bytes, offset, faceCount, sizeof(std::uint32_t) * 3)) {
            return false;
        }

        const std::string targetName = Lowercase(morphName);
        for (std::uint32_t morphIndex = 0; morphIndex < morphCount; ++morphIndex) {
            std::uint32_t nameLength = 0;
            if (!ReadBinaryValue(bytes, offset, nameLength) ||
                offset > bytes.size() ||
                nameLength > bytes.size() - offset) {
                return false;
            }

            std::string name(
                reinterpret_cast<const char*>(bytes.data() + offset),
                nameLength);
            offset += nameLength;
            while (!name.empty() && name.back() == '\0') {
                name.pop_back();
            }

            float scale = 0.0f;
            if (!ReadBinaryValue(bytes, offset, scale)) {
                return false;
            }

            if (Lowercase(name) != targetName) {
                if (!SkipBinaryValues(bytes, offset, vertexCount, sizeof(std::int16_t) * 3)) {
                    return false;
                }
                continue;
            }

            morph.deltas.resize(vertexCount);
            for (RE::NiPoint3& delta : morph.deltas) {
                std::int16_t x = 0;
                std::int16_t y = 0;
                std::int16_t z = 0;
                if (!ReadBinaryValue(bytes, offset, x) ||
                    !ReadBinaryValue(bytes, offset, y) ||
                    !ReadBinaryValue(bytes, offset, z)) {
                    morph.deltas.clear();
                    return false;
                }
                delta = {
                    static_cast<float>(x) * scale,
                    static_cast<float>(y) * scale,
                    static_cast<float>(z) * scale
                };
            }
            return true;
        }

        logger::warn("TRI morph {} was not found in {}", morphName, resolvedPath);
        return false;
    }

    std::string NormalizeTexturePath(const std::string& path)
    {
        return NormalizeResourcePath(path, "textures");
    }

    std::string GetFaceTintPath(const std::string& nifPath)
    {
        std::string normalizedPath = nifPath;
        std::replace(normalizedPath.begin(), normalizedPath.end(), '/', '\\');

        std::string lowerPath = normalizedPath;
        std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });

        constexpr std::string_view faceGeomMarker = "facegendata\\facegeom\\";
        const std::size_t markerPosition = lowerPath.find(faceGeomMarker);
        if (markerPosition == std::string::npos) {
            return {};
        }

        const std::size_t suffixPosition = markerPosition + faceGeomMarker.size();
        std::string suffix = normalizedPath.substr(suffixPosition);
        const std::size_t extensionPosition = suffix.find_last_of('.');
        if (extensionPosition == std::string::npos) {
            return {};
        }

        suffix.erase(extensionPosition);
        suffix.append(".dds");
        return "textures\\actors\\character\\FaceGenData\\FaceTint\\" + suffix;
    }

    ID3D11ShaderResourceView* LoadTexture(
        ID3D11Device* device,
        const std::string& path,
        bool colorTexture)
    {
        if (!device || path.empty()) {
            return nullptr;
        }

        const std::string resolvedPath = NormalizeTexturePath(path);
        std::vector<std::uint8_t> bytes;
        if (!ReadGameResource(resolvedPath, bytes)) {
            logger::warn("Could not read texture resource {} (resolved as {})", path, resolvedPath);
            return nullptr;
        }

        DirectX::ScratchImage image;
        DirectX::TexMetadata metadata{};
        HRESULT result = DirectX::LoadFromDDSMemory(
            bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE, &metadata, image);
        if (FAILED(result)) {
            result = DirectX::LoadFromWICMemory(
                bytes.data(),
                bytes.size(),
                colorTexture ? DirectX::WIC_FLAGS_FORCE_SRGB : DirectX::WIC_FLAGS_IGNORE_SRGB,
                &metadata,
                image);
        }
        if (FAILED(result)) {
            logger::warn(
                "Could not decode texture resource {}: {:08X}",
                resolvedPath,
                static_cast<std::uint32_t>(result));
            return nullptr;
        }

        ID3D11ShaderResourceView* textureView = nullptr;
        const DirectX::CREATETEX_FLAGS creationFlags = colorTexture
            ? DirectX::CREATETEX_FORCE_SRGB
            : DirectX::CREATETEX_IGNORE_SRGB;
        result = DirectX::CreateShaderResourceViewEx(
            device,
            image.GetImages(),
            image.GetImageCount(),
            metadata,
            D3D11_USAGE_DEFAULT,
            D3D11_BIND_SHADER_RESOURCE,
            0,
            0,
            creationFlags,
            &textureView);
        if (FAILED(result)) {
            logger::warn(
                "Could not create texture resource view for {}: {:08X}",
                resolvedPath,
                static_cast<std::uint32_t>(result));
        }
        return SUCCEEDED(result) ? textureView : nullptr;
    }

    nifly::MatTransform GetShapeTransform(nifly::NifFile& file, nifly::NiShape* shape)
    {
        if (shape && shape->IsSkinned()) {
            nifly::MatTransform globalToSkin;
            if (file.CalcShapeTransformGlobalToSkin(shape, globalToSkin)) {
                return globalToSkin.InverseTransform();
            }
        }

        nifly::MatTransform transform = shape->GetTransformToParent();
        nifly::NiNode* parent = file.GetParentNode(shape);
        while (parent) {
            transform = parent->GetTransformToParent().ComposeTransforms(transform);
            parent = file.GetParentNode(parent);
        }
        return transform;
    }

    bool PoseSkinnedShape(
        nifly::NifFile& file,
        nifly::NiShape* shape,
        const std::vector<nifly::Vector3>& positions,
        const std::vector<nifly::Vector3>* normals,
        std::vector<nifly::Vector3>& posedPositions,
        std::vector<nifly::Vector3>& posedNormals)
    {
        if (!shape || !shape->IsSkinned() || positions.empty()) {
            return false;
        }

        std::vector<std::string> boneNames;
        if (file.GetShapeBoneList(shape, boneNames) == 0) {
            return false;
        }

        posedPositions.assign(positions.size(), {});
        if (normals) {
            posedNormals.assign(positions.size(), {});
        } else {
            posedNormals.clear();
        }
        std::vector<float> accumulatedWeights(positions.size(), 0.0f);
        std::size_t appliedBoneCount = 0;

        for (std::uint32_t boneIndex = 0; boneIndex < boneNames.size(); ++boneIndex) {
            nifly::MatTransform boneToGlobal;
            nifly::MatTransform skinToBone;
            if (!file.GetNodeTransformToGlobal(boneNames[boneIndex], boneToGlobal) ||
                !file.GetShapeTransformSkinToBone(shape, boneIndex, skinToBone)) {
                continue;
            }

            std::unordered_map<std::uint16_t, float> weights;
            if (file.GetShapeBoneWeights(shape, boneIndex, weights) == 0) {
                continue;
            }

            const nifly::MatTransform skinToGlobal = boneToGlobal.ComposeTransforms(skinToBone);
            for (const auto& [vertexIndex, weight] : weights) {
                if (vertexIndex >= positions.size() || weight <= 0.0f) {
                    continue;
                }

                posedPositions[vertexIndex] += skinToGlobal.ApplyTransform(positions[vertexIndex]) * weight;
                if (normals && vertexIndex < normals->size()) {
                    posedNormals[vertexIndex] += skinToGlobal.ApplyTransformToDir(normals->at(vertexIndex)) * weight;
                }
                accumulatedWeights[vertexIndex] += weight;
            }
            ++appliedBoneCount;
        }

        if (appliedBoneCount == 0) {
            posedPositions.clear();
            posedNormals.clear();
            return false;
        }

        const nifly::MatTransform fallbackTransform = GetShapeTransform(file, shape);
        for (std::size_t vertexIndex = 0; vertexIndex < positions.size(); ++vertexIndex) {
            const float weight = accumulatedWeights[vertexIndex];
            if (weight > 0.00001f) {
                posedPositions[vertexIndex] /= weight;
                if (normals && vertexIndex < normals->size()) {
                    posedNormals[vertexIndex] /= weight;
                    posedNormals[vertexIndex].Normalize();
                }
            } else {
                posedPositions[vertexIndex] = fallbackTransform.ApplyTransform(positions[vertexIndex]);
                if (normals && vertexIndex < normals->size()) {
                    posedNormals[vertexIndex] = fallbackTransform.ApplyTransformToDir(normals->at(vertexIndex));
                    posedNormals[vertexIndex].Normalize();
                }
            }
        }

        logger::info(
            "nifly posed skinned shape {} using {} bone(s)",
            shape->name.get(),
            appliedBoneCount);
        return true;
    }

    bool IsShapeVisible(nifly::NifFile& file, nifly::NiShape* shape)
    {
        constexpr std::uint32_t appCulledFlag = 1u;
        nifly::NiAVObject* object = shape;
        while (object) {
            if ((object->flags & appCulledFlag) != 0) {
                return false;
            }
            object = file.GetParentNode(object);
        }
        return true;
    }

    RE::NiMatrix3 GetInventoryRotation(nifly::NifFile& file)
    {
        RE::NiMatrix3 rotation;
        nifly::NiHeader& header = file.GetHeader();
        for (std::uint32_t index = 0; index < header.GetNumBlocks(); ++index) {
            nifly::BSInvMarker* marker = header.GetBlock<nifly::BSInvMarker>(index);
            if (!marker) {
                continue;
            }

            rotation.SetEulerAnglesXYZ(
                RE::fixed_range_to_radians(marker->rotationX),
                RE::fixed_range_to_radians(marker->rotationY),
                RE::fixed_range_to_radians(marker->rotationZ));
            break;
        }
        return rotation;
    }

    RE::NiPoint3 TransformPoint(
        const RE::NiMatrix3& inventoryRotation,
        const nifly::Vector3& point)
    {
        return inventoryRotation * RE::NiPoint3{point.x, point.y, point.z};
    }

    RE::NiPoint3 TransformNormal(
        const RE::NiMatrix3& inventoryRotation,
        const nifly::Vector3& normal)
    {
        RE::NiPoint3 result = inventoryRotation * RE::NiPoint3{normal.x, normal.y, normal.z};
        const float length = std::sqrt(result.x * result.x + result.y * result.y + result.z * result.z);
        if (length > 0.00001f) {
            result /= length;
        }
        return result;
    }

    float DotProduct(const RE::NiPoint3& left, const RE::NiPoint3& right)
    {
        return left.x * right.x + left.y * right.y + left.z * right.z;
    }

    RE::NiPoint3 CrossProduct(const RE::NiPoint3& left, const RE::NiPoint3& right)
    {
        return {
            left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x
        };
    }

    MeshBoneFrame CreateBoneFrame(
        const RE::NiMatrix3& inventoryRotation,
        const nifly::MatTransform& boneToGlobal)
    {
        const nifly::Vector3 origin = boneToGlobal.ApplyTransform({0.0f, 0.0f, 0.0f});
        const nifly::Vector3 x = boneToGlobal.ApplyTransform({1.0f, 0.0f, 0.0f});
        const nifly::Vector3 y = boneToGlobal.ApplyTransform({0.0f, 1.0f, 0.0f});
        const nifly::Vector3 z = boneToGlobal.ApplyTransform({0.0f, 0.0f, 1.0f});

        MeshBoneFrame frame;
        frame.origin = TransformPoint(inventoryRotation, origin);
        frame.axisX = TransformPoint(inventoryRotation, x) - frame.origin;
        frame.axisY = TransformPoint(inventoryRotation, y) - frame.origin;
        frame.axisZ = TransformPoint(inventoryRotation, z) - frame.origin;
        return frame;
    }

    RE::NiPoint3 TransformBetweenBoneFrames(
        const RE::NiPoint3& value,
        const MeshBoneFrame& source,
        const MeshBoneFrame& target,
        bool direction)
    {
        const RE::NiPoint3 relativeValue = direction ? value : value - source.origin;
        const RE::NiPoint3 axisYCrossAxisZ = CrossProduct(source.axisY, source.axisZ);
        const float determinant = DotProduct(source.axisX, axisYCrossAxisZ);
        if (std::abs(determinant) <= 0.00001f) {
            return value;
        }

        const RE::NiPoint3 local{
            DotProduct(relativeValue, axisYCrossAxisZ) / determinant,
            DotProduct(relativeValue, CrossProduct(source.axisZ, source.axisX)) / determinant,
            DotProduct(relativeValue, CrossProduct(source.axisX, source.axisY)) / determinant
        };
        RE::NiPoint3 transformed =
            target.axisX * local.x +
            target.axisY * local.y +
            target.axisZ * local.z;
        if (!direction) {
            transformed += target.origin;
        }
        return transformed;
    }

    RE::NiPoint3 NormalizeDirection(RE::NiPoint3 direction)
    {
        const float length = std::sqrt(DotProduct(direction, direction));
        if (length > 0.00001f) {
            direction /= length;
        }
        return direction;
    }

    RE::NiPoint3 ApplyBoneFrame(
        const MeshBoneFrame& frame,
        const RE::NiPoint3& value,
        bool direction)
    {
        RE::NiPoint3 result =
            frame.axisX * value.x +
            frame.axisY * value.y +
            frame.axisZ * value.z;
        if (!direction) {
            result += frame.origin;
        }
        return result;
    }

    MeshBoneFrame CreateBoneFrame(const MeshRenderingFrameworkAPI::BoneTransform& transform)
    {
        float x = transform.rotation[0];
        float y = transform.rotation[1];
        float z = transform.rotation[2];
        float w = transform.rotation[3];
        const float quaternionLength = std::sqrt(x * x + y * y + z * z + w * w);
        if (quaternionLength > 0.00001f) {
            x /= quaternionLength;
            y /= quaternionLength;
            z /= quaternionLength;
            w /= quaternionLength;
        } else {
            x = 0.0f;
            y = 0.0f;
            z = 0.0f;
            w = 1.0f;
        }

        MeshBoneFrame frame;
        frame.origin = {
            transform.translation[0],
            transform.translation[1],
            transform.translation[2]
        };
        frame.axisX = RE::NiPoint3{
            1.0f - 2.0f * (y * y + z * z),
            2.0f * (x * y + z * w),
            2.0f * (x * z - y * w)
        } * transform.scale[0];
        frame.axisY = RE::NiPoint3{
            2.0f * (x * y - z * w),
            1.0f - 2.0f * (x * x + z * z),
            2.0f * (y * z + x * w)
        } * transform.scale[1];
        frame.axisZ = RE::NiPoint3{
            2.0f * (x * z + y * w),
            2.0f * (y * z - x * w),
            1.0f - 2.0f * (x * x + y * y)
        } * transform.scale[2];
        return frame;
    }

    MeshBoneFrame ComposeBoneFrames(const MeshBoneFrame& parent, const MeshBoneFrame& local)
    {
        MeshBoneFrame result;
        result.origin = ApplyBoneFrame(parent, local.origin, false);
        result.axisX = ApplyBoneFrame(parent, local.axisX, true);
        result.axisY = ApplyBoneFrame(parent, local.axisY, true);
        result.axisZ = ApplyBoneFrame(parent, local.axisZ, true);
        return result;
    }

    MeshBoneFrame RotateBoneFrame(
        const RE::NiMatrix3& rotation,
        const MeshBoneFrame& frame)
    {
        MeshBoneFrame result;
        result.origin = rotation * frame.origin;
        result.axisX = rotation * frame.axisX;
        result.axisY = rotation * frame.axisY;
        result.axisZ = rotation * frame.axisZ;
        return result;
    }

    std::size_t TextureSlotIndex(MeshTextureSlot slot)
    {
        return static_cast<std::size_t>(slot);
    }

    void CalculateTangents(MeshPart& part)
    {
        std::vector<RE::NiPoint3> tangentSums(part.vertices.size());
        std::vector<RE::NiPoint3> bitangentSums(part.vertices.size());

        for (std::size_t index = 0; index + 2 < part.indices.size(); index += 3) {
            const std::uint16_t index0 = part.indices[index];
            const std::uint16_t index1 = part.indices[index + 1];
            const std::uint16_t index2 = part.indices[index + 2];
            const MeshVertex& vertex0 = part.vertices[index0];
            const MeshVertex& vertex1 = part.vertices[index1];
            const MeshVertex& vertex2 = part.vertices[index2];

            const RE::NiPoint3 edge1{
                vertex1.position[0] - vertex0.position[0],
                vertex1.position[1] - vertex0.position[1],
                vertex1.position[2] - vertex0.position[2]
            };
            const RE::NiPoint3 edge2{
                vertex2.position[0] - vertex0.position[0],
                vertex2.position[1] - vertex0.position[1],
                vertex2.position[2] - vertex0.position[2]
            };
            const float deltaU1 = vertex1.uv[0] - vertex0.uv[0];
            const float deltaV1 = vertex1.uv[1] - vertex0.uv[1];
            const float deltaU2 = vertex2.uv[0] - vertex0.uv[0];
            const float deltaV2 = vertex2.uv[1] - vertex0.uv[1];
            const float determinant = deltaU1 * deltaV2 - deltaU2 * deltaV1;
            if (std::abs(determinant) < 0.000001f) {
                continue;
            }

            const float reciprocal = 1.0f / determinant;
            const RE::NiPoint3 tangent = (edge1 * deltaV2 - edge2 * deltaV1) * reciprocal;
            const RE::NiPoint3 bitangent = (edge2 * deltaU1 - edge1 * deltaU2) * reciprocal;
            for (const std::uint16_t vertexIndex : {index0, index1, index2}) {
                tangentSums[vertexIndex] += tangent;
                bitangentSums[vertexIndex] += bitangent;
            }
        }

        for (std::size_t index = 0; index < part.vertices.size(); ++index) {
            MeshVertex& vertex = part.vertices[index];
            const RE::NiPoint3 normal{vertex.normal[0], vertex.normal[1], vertex.normal[2]};
            RE::NiPoint3 tangent = tangentSums[index] - normal * normal.Dot(tangentSums[index]);
            if (tangent.SqrLength() < 0.000001f) {
                const RE::NiPoint3 reference = std::abs(normal.z) < 0.999f
                    ? RE::NiPoint3{0.0f, 0.0f, 1.0f}
                    : RE::NiPoint3{0.0f, 1.0f, 0.0f};
                tangent = reference.Cross(normal);
            }
            tangent.Unitize();
            const RE::NiPoint3 generatedBitangent = normal.Cross(tangent);
            const float handedness = generatedBitangent.Dot(bitangentSums[index]) < 0.0f ? -1.0f : 1.0f;
            vertex.tangent[0] = tangent.x;
            vertex.tangent[1] = tangent.y;
            vertex.tangent[2] = tangent.z;
            vertex.tangent[3] = handedness;
        }
    }
}

MeshPart::MeshPart(MeshPart&& other) noexcept
    : shapeName(std::move(other.shapeName)),
      sourcePath(std::move(other.sourcePath)),
      vertices(std::move(other.vertices)),
      indices(std::move(other.indices)),
      texturePaths(std::move(other.texturePaths)),
      vertexBuffer(std::exchange(other.vertexBuffer, nullptr)),
      indexBuffer(std::exchange(other.indexBuffer, nullptr)),
      textureViews(std::exchange(
          other.textureViews,
          std::array<ID3D11ShaderResourceView*, MeshTextureSlotCount>{})),
      textureLoadAttempted(std::exchange(
          other.textureLoadAttempted,
          std::array<bool, MeshTextureSlotCount>{})),
      alphaMode(other.alphaMode),
      alphaThreshold(other.alphaThreshold),
      materialAlpha(other.materialAlpha),
      specularStrength(other.specularStrength),
      glossiness(other.glossiness),
      emissiveMultiple(other.emissiveMultiple),
      environmentScale(other.environmentScale),
      parallaxScale(other.parallaxScale),
      backlightPower(other.backlightPower),
      rimlightPower(other.rimlightPower),
      auxiliaryMapMode(other.auxiliaryMapMode),
      modelSpaceNormals(other.modelSpaceNormals),
      useVertexColors(other.useVertexColors),
      useVertexAlpha(other.useVertexAlpha),
      emissiveEnabled(other.emissiveEnabled),
      specularEnabled(other.specularEnabled),
      environmentEnabled(other.environmentEnabled),
      faceOrSkin(other.faceOrSkin),
      hairMaterial(other.hairMaterial),
      skinTinted(other.skinTinted),
      faceGenPart(other.faceGenPart),
      vertexBufferDirty(other.vertexBufferDirty),
      center(other.center),
      bindVertices(std::move(other.bindVertices)),
      morphBaseVertices(std::move(other.morphBaseVertices)),
      morphSourcePositions(std::move(other.morphSourcePositions)),
      morphToBindFrame(other.morphToBindFrame),
      skinBoneNames(std::move(other.skinBoneNames)),
      bindBoneFrames(std::move(other.bindBoneFrames)),
      morphToBindBoneFrames(std::move(other.morphToBindBoneFrames)),
      skinWeights(std::move(other.skinWeights))
{
    std::copy(std::begin(other.specularColor), std::end(other.specularColor), specularColor);
    std::copy(std::begin(other.emissiveColor), std::end(other.emissiveColor), emissiveColor);
    std::copy(std::begin(other.tintColor), std::end(other.tintColor), tintColor);
    std::copy(std::begin(other.uvScale), std::end(other.uvScale), uvScale);
    std::copy(std::begin(other.uvOffset), std::end(other.uvOffset), uvOffset);
}

MeshPart& MeshPart::operator=(MeshPart&& other) noexcept
{
    if (this != &other) {
        ResetGpuResources();
        shapeName = std::move(other.shapeName);
        sourcePath = std::move(other.sourcePath);
        vertices = std::move(other.vertices);
        indices = std::move(other.indices);
        texturePaths = std::move(other.texturePaths);
        vertexBuffer = std::exchange(other.vertexBuffer, nullptr);
        indexBuffer = std::exchange(other.indexBuffer, nullptr);
        textureViews = std::exchange(
            other.textureViews,
            std::array<ID3D11ShaderResourceView*, MeshTextureSlotCount>{});
        textureLoadAttempted = std::exchange(
            other.textureLoadAttempted,
            std::array<bool, MeshTextureSlotCount>{});
        alphaMode = other.alphaMode;
        alphaThreshold = other.alphaThreshold;
        materialAlpha = other.materialAlpha;
        std::copy(std::begin(other.specularColor), std::end(other.specularColor), specularColor);
        specularStrength = other.specularStrength;
        glossiness = other.glossiness;
        std::copy(std::begin(other.emissiveColor), std::end(other.emissiveColor), emissiveColor);
        emissiveMultiple = other.emissiveMultiple;
        std::copy(std::begin(other.tintColor), std::end(other.tintColor), tintColor);
        environmentScale = other.environmentScale;
        std::copy(std::begin(other.uvScale), std::end(other.uvScale), uvScale);
        std::copy(std::begin(other.uvOffset), std::end(other.uvOffset), uvOffset);
        parallaxScale = other.parallaxScale;
        backlightPower = other.backlightPower;
        rimlightPower = other.rimlightPower;
        auxiliaryMapMode = other.auxiliaryMapMode;
        modelSpaceNormals = other.modelSpaceNormals;
        useVertexColors = other.useVertexColors;
        useVertexAlpha = other.useVertexAlpha;
        emissiveEnabled = other.emissiveEnabled;
        specularEnabled = other.specularEnabled;
        environmentEnabled = other.environmentEnabled;
        faceOrSkin = other.faceOrSkin;
        hairMaterial = other.hairMaterial;
        skinTinted = other.skinTinted;
        faceGenPart = other.faceGenPart;
        vertexBufferDirty = other.vertexBufferDirty;
        center = other.center;
        bindVertices = std::move(other.bindVertices);
        morphBaseVertices = std::move(other.morphBaseVertices);
        morphSourcePositions = std::move(other.morphSourcePositions);
        morphToBindFrame = other.morphToBindFrame;
        skinBoneNames = std::move(other.skinBoneNames);
        bindBoneFrames = std::move(other.bindBoneFrames);
        morphToBindBoneFrames = std::move(other.morphToBindBoneFrames);
        skinWeights = std::move(other.skinWeights);
    }
    return *this;
}

MeshPart::~MeshPart()
{
    ResetGpuResources();
}

void MeshPart::ResetGpuResources()
{
    for (ID3D11ShaderResourceView*& textureView : textureViews) {
        ReleaseResource(textureView);
    }
    ReleaseResource(indexBuffer);
    ReleaseResource(vertexBuffer);
    textureLoadAttempted.fill(false);
    vertexBufferDirty = false;
}

void Mesh::Fit(RE::NiPoint2 position, RE::NiPoint2 rectSize)
{
    const RE::NiPoint2 renderSize{
        static_cast<float>(mesh->width),
        static_cast<float>(mesh->height)
    };
    const RE::NiPoint2 screenRatio = MeshMath::PositionToScreenRatio(position + rectSize / 2, renderSize);
    constexpr float horizontalHalfSpan = 130.0f;
    const float aspect = renderSize.x / renderSize.y;
    const float verticalHalfSpan = horizontalHalfSpan / aspect;
    mesh->position = RE::NiPoint3{
        -screenRatio.x * horizontalHalfSpan,
        -500.0f,
        -screenRatio.y * verticalHalfSpan
    };

    const float availableHalfWidth = horizontalHalfSpan * (rectSize.x / renderSize.x);
    const float availableHalfHeight = verticalHalfSpan * (rectSize.y / renderSize.y);
    constexpr float fitMargin = 0.9f;
    constexpr float cameraDistance = 820.0f;
    const float limitingHalfSpan = std::min(availableHalfWidth, availableHalfHeight);
    const float fittedRadius = fitMargin * limitingHalfSpan * cameraDistance /
                               (cameraDistance + fitMargin * limitingHalfSpan);
    mesh->scale = boundingRadius > 0.00001f
        ? fittedRadius / boundingRadius
        : 1.0f;
}

void Mesh::Initialize(uint32_t width, uint32_t height)
{
    mesh = new MeshRenderingFrameworkAPI::Internal::IMesh();
    mesh->width = width;
    mesh->height = height;
    mesh->id = autoIncrement++;
}

bool Mesh::Load(const char* nifPath)
{
    if (!nifPath || !nifPath[0]) {
        return false;
    }
    sourcePath = NormalizeNifPath(nifPath);

    std::vector<std::uint8_t> bytes;
    if (!ReadGameResource(sourcePath, bytes)) {
        logger::error("nifly could not open NIF resource: {} (resolved as {})", nifPath, sourcePath);
        return false;
    }

    const std::string nifData(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::istringstream stream(nifData, std::ios::in | std::ios::binary);
    nifly::NifFile file;
    if (file.Load(stream) != 0 || !file.IsValid()) {
        logger::error("nifly could not parse NIF resource: {}", sourcePath);
        return false;
    }

    const RE::NiMatrix3 inventoryRotation = GetInventoryRotation(file);
    animationRotation = inventoryRotation;
    boneFrames.clear();
    const std::string faceTintPath = GetFaceTintPath(sourcePath);
    const bool hasFaceTintTexture = !faceTintPath.empty() && GameResourceExists(faceTintPath);
    RE::NiPoint3 minimum{};
    RE::NiPoint3 maximum{};
    bool hasBounds = false;
    std::size_t totalVertexCount = 0;
    std::size_t totalTriangleCount = 0;

    for (nifly::NiShape* shape : file.GetShapes()) {
        if (!shape || !IsShapeVisible(file, shape)) {
            continue;
        }

        // Head and FaceGen meshes commonly use BSDynamicTriShape, which keeps
        // positions in dynamicData and intentionally clears the VF_VERTEX flag.
        const bool hasReadableVertices = shape->HasVertices() ||
                                         dynamic_cast<nifly::BSDynamicTriShape*>(shape) != nullptr;
        if (!hasReadableVertices) {
            continue;
        }

        std::vector<nifly::Vector3> dynamicPositions;
        const std::vector<nifly::Vector3>* positions = nullptr;
        nifly::BSDynamicTriShape* dynamicShape =
            dynamic_cast<nifly::BSDynamicTriShape*>(shape);
        if (dynamicShape && !dynamicShape->dynamicData.empty()) {
            dynamicPositions.reserve(dynamicShape->dynamicData.size());
            for (const nifly::Vector4& dynamicPosition : dynamicShape->dynamicData) {
                dynamicPositions.push_back({
                    dynamicPosition.x,
                    dynamicPosition.y,
                    dynamicPosition.z
                });
            }
            positions = &dynamicPositions;
        } else {
            positions = file.GetVertsForShape(shape);
        }
        if (!positions || positions->empty()) {
            continue;
        }

        std::vector<nifly::Triangle> triangles;
        if (!shape->GetTriangles(triangles) || triangles.empty()) {
            continue;
        }

        const std::vector<nifly::Vector3>* normals = file.GetNormalsForShape(shape);
        const std::vector<nifly::Vector2>* uvs = file.GetUvsForShape(shape);
        const std::vector<nifly::Color4>* colors = file.GetColorsForShape(shape);
        std::vector<std::string> boneNames;
        if (file.GetShapeBoneList(shape, boneNames) > 0) {
            for (const std::string& boneName : boneNames) {
                if (boneFrames.contains(boneName)) {
                    continue;
                }

                nifly::MatTransform boneToGlobal;
                if (file.GetNodeTransformToGlobal(boneName, boneToGlobal)) {
                    boneFrames.emplace(boneName, CreateBoneFrame(inventoryRotation, boneToGlobal));
                }
            }
        }
        const nifly::MatTransform transform = GetShapeTransform(file, shape);
        std::vector<nifly::Vector3> posedPositions;
        std::vector<nifly::Vector3> posedNormals;
        const bool hasSkinPose = PoseSkinnedShape(
            file,
            shape,
            *positions,
            normals,
            posedPositions,
            posedNormals);

        MeshPart part;
        part.shapeName = Lowercase(shape->name.get());
        part.sourcePath = sourcePath;
        part.faceGenPart = !faceTintPath.empty();
        part.morphToBindFrame = CreateBoneFrame(inventoryRotation, transform);
        part.vertices.reserve(positions->size());
        for (std::size_t index = 0; index < positions->size(); ++index) {
            const nifly::Vector3 transformedPosition = hasSkinPose
                ? posedPositions[index]
                : transform.ApplyTransform(positions->at(index));
            const RE::NiPoint3 position = TransformPoint(inventoryRotation, transformedPosition);

            nifly::Vector3 transformedNormal;
            if (normals && index < normals->size()) {
                transformedNormal = hasSkinPose
                    ? posedNormals[index]
                    : transform.ApplyTransformToDir(normals->at(index));
            }
            const RE::NiPoint3 normal = normals && index < normals->size()
                ? TransformNormal(inventoryRotation, transformedNormal)
                : RE::NiPoint3{0.0f, 1.0f, 0.0f};

            MeshVertex vertex{};
            vertex.position[0] = position.x;
            vertex.position[1] = position.y;
            vertex.position[2] = position.z;
            vertex.normal[0] = normal.x;
            vertex.normal[1] = normal.y;
            vertex.normal[2] = normal.z;

            if (uvs && index < uvs->size()) {
                vertex.uv[0] = uvs->at(index).u;
                vertex.uv[1] = uvs->at(index).v;
            }
            if (colors && index < colors->size()) {
                vertex.color[0] = colors->at(index).r;
                vertex.color[1] = colors->at(index).g;
                vertex.color[2] = colors->at(index).b;
                vertex.color[3] = colors->at(index).a;
            }
            part.vertices.push_back(vertex);
        }

        if (hasSkinPose) {
            for (std::uint32_t sourceBoneIndex = 0; sourceBoneIndex < boneNames.size(); ++sourceBoneIndex) {
                nifly::MatTransform boneToGlobal;
                nifly::MatTransform skinToBone;
                if (!file.GetNodeTransformToGlobal(boneNames[sourceBoneIndex], boneToGlobal) ||
                    !file.GetShapeTransformSkinToBone(shape, sourceBoneIndex, skinToBone)) {
                    continue;
                }

                std::unordered_map<std::uint16_t, float> weights;
                if (file.GetShapeBoneWeights(shape, sourceBoneIndex, weights) == 0) {
                    continue;
                }

                const std::uint16_t partBoneIndex =
                    static_cast<std::uint16_t>(part.skinBoneNames.size());
                part.skinBoneNames.push_back(boneNames[sourceBoneIndex]);
                part.bindBoneFrames.push_back(CreateBoneFrame(inventoryRotation, boneToGlobal));
                const nifly::MatTransform skinToGlobal = boneToGlobal.ComposeTransforms(skinToBone);
                part.morphToBindBoneFrames.push_back(
                    CreateBoneFrame(inventoryRotation, skinToGlobal));
                for (const auto& [vertexIndex, weight] : weights) {
                    if (vertexIndex >= part.vertices.size() || weight <= 0.0f) {
                        continue;
                    }
                    part.skinWeights.push_back({vertexIndex, partBoneIndex, weight});
                }
            }
        }

        RE::NiPoint3 partMinimum{
            part.vertices.front().position[0],
            part.vertices.front().position[1],
            part.vertices.front().position[2]
        };
        RE::NiPoint3 partMaximum = partMinimum;
        for (const MeshVertex& vertex : part.vertices) {
            partMinimum.x = std::min(partMinimum.x, vertex.position[0]);
            partMinimum.y = std::min(partMinimum.y, vertex.position[1]);
            partMinimum.z = std::min(partMinimum.z, vertex.position[2]);
            partMaximum.x = std::max(partMaximum.x, vertex.position[0]);
            partMaximum.y = std::max(partMaximum.y, vertex.position[1]);
            partMaximum.z = std::max(partMaximum.z, vertex.position[2]);
        }
        part.center = (partMinimum + partMaximum) * 0.5f;

        constexpr std::uint16_t alphaBlendFlag = 1u << 0;
        constexpr std::uint16_t alphaTestFlag = 1u << 9;
        nifly::NiAlphaProperty* alphaProperty = file.GetAlphaProperty(shape);
        if (alphaProperty) {
            const bool alphaBlend = (alphaProperty->flags & alphaBlendFlag) != 0;
            const bool alphaTest = (alphaProperty->flags & alphaTestFlag) != 0;
            // Hair commonly enables both blending and testing. Prefer the
            // tested, depth-writing pass so overlapping hair cards do not
            // expose their individual triangles through one another.
            part.alphaMode = alphaTest
                ? MeshAlphaMode::Cutout
                : (alphaBlend ? MeshAlphaMode::Blend : MeshAlphaMode::Opaque);
            part.alphaThreshold = alphaTest
                ? static_cast<float>(alphaProperty->threshold) / 255.0f
                : -1.0f;
        }

        nifly::NiShader* shader = file.GetShader(shape);
        if (shader) {
            part.materialAlpha = std::clamp(shader->GetAlpha(), 0.0f, 1.0f);
            if (part.materialAlpha < 0.999f && part.alphaMode != MeshAlphaMode::Cutout) {
                part.alphaMode = MeshAlphaMode::Blend;
            }

            const nifly::Vector3 specularColor = shader->GetSpecularColor();
            part.specularColor[0] = specularColor.x;
            part.specularColor[1] = specularColor.y;
            part.specularColor[2] = specularColor.z;
            part.specularStrength = std::max(shader->GetSpecularStrength(), 0.0f);
            part.glossiness = std::clamp(shader->GetGlossiness(), 1.0f, 128.0f);

            const nifly::Color4 emissiveColor = shader->GetEmissiveColor();
            part.emissiveColor[0] = emissiveColor.r;
            part.emissiveColor[1] = emissiveColor.g;
            part.emissiveColor[2] = emissiveColor.b;
            part.emissiveMultiple = std::max(shader->GetEmissiveMultiple(), 0.0f);
            part.environmentScale = std::max(shader->GetEnvironmentMapScale(), 0.0f);
            part.backlightPower = std::max(shader->GetBacklightPower(), 0.0f);
            part.rimlightPower = std::max(shader->GetRimlightPower(), 0.01f);
            part.modelSpaceNormals = shader->IsModelSpace();
            part.useVertexColors = shader->HasVertexColors();
            part.useVertexAlpha = shader->HasVertexAlpha();
            part.emissiveEnabled = shader->IsEmissive() || shader->HasGlowmap();
            part.specularEnabled = shader->HasSpecular();
            part.environmentEnabled = shader->HasEnvironmentMapping();

            const nifly::Vector2 uvScale = shader->GetUVScale();
            const nifly::Vector2 uvOffset = shader->GetUVOffset();
            part.uvScale[0] = uvScale.u;
            part.uvScale[1] = uvScale.v;
            part.uvOffset[0] = uvOffset.u;
            part.uvOffset[1] = uvOffset.v;

            nifly::BSLightingShaderProperty* lightingShader =
                dynamic_cast<nifly::BSLightingShaderProperty*>(shader);
            if (lightingShader) {
                const std::uint32_t shaderType = lightingShader->GetShaderType();
                part.hairMaterial = shaderType == nifly::BSLSP_HAIRTINT;
                part.skinTinted = lightingShader->IsSkinTinted();
                const bool faceOrSkinTinted =
                    lightingShader->IsFaceTinted() ||
                    lightingShader->IsSkinTinted() ||
                    (lightingShader->shaderFlags1 & nifly::SLSF1_FACEGEN_RGB_TINT) != 0;
                part.faceOrSkin = faceOrSkinTinted;
                part.faceGenPart = part.faceGenPart ||
                                   lightingShader->IsFaceTinted() ||
                                   (lightingShader->shaderFlags1 & nifly::SLSF1_FACEGEN_RGB_TINT) != 0;
                if (faceOrSkinTinted) {
                    // The external skin lighting map carries subtle variation;
                    // it still needs a dielectric base strength to remain visible.
                    part.specularStrength = std::max(part.specularStrength, 0.5f);
                    if (part.specularColor[0] + part.specularColor[1] + part.specularColor[2] < 0.0001f) {
                        part.specularColor[0] = 1.0f;
                        part.specularColor[1] = 1.0f;
                        part.specularColor[2] = 1.0f;
                    }
                }
                const bool hasParallax = shaderType == nifly::BSLSP_PARALLAX ||
                                         shaderType == nifly::BSLSP_PARALLAXOCC ||
                                         shaderType == nifly::BSLSP_MULTILAYERPARALLAX;
                if (hasParallax) {
                    part.parallaxScale = lightingShader->parallaxInnerLayerThickness > 0.0f
                        ? std::clamp(lightingShader->parallaxInnerLayerThickness, 0.005f, 0.1f)
                        : 0.03f;
                }

                if (lightingShader->HasGlowmap()) {
                    part.auxiliaryMapMode = 1.0f;
                    if (part.emissiveColor[0] + part.emissiveColor[1] + part.emissiveColor[2] < 0.0001f) {
                        part.emissiveColor[0] = 1.0f;
                        part.emissiveColor[1] = 1.0f;
                        part.emissiveColor[2] = 1.0f;
                    }
                    part.emissiveMultiple = std::max(part.emissiveMultiple, 1.0f);
                } else if (lightingShader->IsSkinTinted() || lightingShader->IsFaceTinted() ||
                           shaderType == nifly::BSLSP_HAIRTINT) {
                    const nifly::Vector3 tint = shaderType == nifly::BSLSP_HAIRTINT
                        ? lightingShader->hairTintColor
                        : lightingShader->skinTintColor;
                    part.tintColor[0] = tint.x;
                    part.tintColor[1] = tint.y;
                    part.tintColor[2] = tint.z;
                    part.auxiliaryMapMode = 2.0f;
                } else if (lightingShader->HasSoftlight() || lightingShader->HasRimlight()) {
                    part.auxiliaryMapMode = 3.0f;
                }
                part.environmentEnabled = part.environmentEnabled ||
                                          shaderType == nifly::BSLSP_ENVMAP ||
                                          shaderType == nifly::BSLSP_EYE;
            }
        }

        part.indices.reserve(triangles.size() * 3);
        for (const nifly::Triangle& triangle : triangles) {
            if (triangle.p1 >= part.vertices.size() ||
                triangle.p2 >= part.vertices.size() ||
                triangle.p3 >= part.vertices.size()) {
                continue;
            }
            part.indices.push_back(triangle.p1);
            part.indices.push_back(triangle.p2);
            part.indices.push_back(triangle.p3);
        }
        if (part.indices.empty()) {
            continue;
        }
        CalculateTangents(part);

        for (const MeshVertex& vertex : part.vertices) {
            const RE::NiPoint3 position{
                vertex.position[0],
                vertex.position[1],
                vertex.position[2]
            };
            if (!hasBounds) {
                minimum = position;
                maximum = position;
                hasBounds = true;
            } else {
                minimum.x = std::min(minimum.x, position.x);
                minimum.y = std::min(minimum.y, position.y);
                minimum.z = std::min(minimum.z, position.z);
                maximum.x = std::max(maximum.x, position.x);
                maximum.y = std::max(maximum.y, position.y);
                maximum.z = std::max(maximum.z, position.z);
            }
        }

        std::array<std::string, MeshTextureSlotCount> rawTexturePaths;
        std::array<std::uint32_t, MeshTextureSlotCount> textureSources{};
        for (std::size_t textureIndex = 0; textureIndex < MeshTextureSlotCount; ++textureIndex) {
            textureSources[textureIndex] = file.GetTextureSlot(
                shape,
                rawTexturePaths[textureIndex],
                static_cast<std::uint32_t>(textureIndex));
        }

        // Older NiTexturingProperty slots have different meanings. Convert them to
        // the Bethesda shader layout used by the renderer.
        if (textureSources[0] == 3) {
            part.texturePaths[TextureSlotIndex(MeshTextureSlot::Diffuse)] = rawTexturePaths[0];
            part.texturePaths[TextureSlotIndex(MeshTextureSlot::Subsurface)] = rawTexturePaths[2];
            part.texturePaths[TextureSlotIndex(MeshTextureSlot::Specular)] = rawTexturePaths[3];
            part.texturePaths[TextureSlotIndex(MeshTextureSlot::Glow)] = rawTexturePaths[4];
            part.texturePaths[TextureSlotIndex(MeshTextureSlot::Normal)] = rawTexturePaths[5];
            if (!rawTexturePaths[4].empty()) {
                part.auxiliaryMapMode = 1.0f;
                part.emissiveEnabled = true;
                part.emissiveColor[0] = 1.0f;
                part.emissiveColor[1] = 1.0f;
                part.emissiveColor[2] = 1.0f;
                part.emissiveMultiple = std::max(part.emissiveMultiple, 1.0f);
            }
        } else {
            part.texturePaths = std::move(rawTexturePaths);
        }

        if (part.modelSpaceNormals) {
            // Bethesda shader slot 7 is context-sensitive: model-space-normal
            // materials use it as an external specular map, while other
            // materials use it as a backlight mask.
            const std::size_t backlightSlot = TextureSlotIndex(MeshTextureSlot::Backlight);
            const std::size_t specularSlot = TextureSlotIndex(MeshTextureSlot::Specular);
            part.texturePaths[specularSlot] = std::move(part.texturePaths[backlightSlot]);
            part.texturePaths[backlightSlot].clear();
            if (!part.texturePaths[specularSlot].empty()) {
                part.specularEnabled = true;
            }
        }

        nifly::BSLightingShaderProperty* lightingShader =
            dynamic_cast<nifly::BSLightingShaderProperty*>(shader);
        if (hasFaceTintTexture && lightingShader && part.faceOrSkin) {
            // FaceTint is a neutral detail/tint layer. The face's actual base
            // diffuse remains texture slot 0 and the two are combined in HLSL.
            part.texturePaths[TextureSlotIndex(MeshTextureSlot::FaceTint)] = faceTintPath;
            part.texturePaths[TextureSlotIndex(MeshTextureSlot::Environment)].clear();
            part.faceOrSkin = true;
        }

        nifly::BSEffectShaderProperty* effectShader =
            dynamic_cast<nifly::BSEffectShaderProperty*>(shader);
        if (effectShader) {
            const std::size_t glowSlot = TextureSlotIndex(MeshTextureSlot::Glow);
            const std::size_t heightSlot = TextureSlotIndex(MeshTextureSlot::Height);
            const std::size_t subsurfaceSlot = TextureSlotIndex(MeshTextureSlot::Subsurface);
            const std::size_t backlightSlot = TextureSlotIndex(MeshTextureSlot::Backlight);
            const std::size_t specularSlot = TextureSlotIndex(MeshTextureSlot::Specular);

            part.texturePaths[subsurfaceSlot] = std::move(part.texturePaths[heightSlot]);
            part.texturePaths[heightSlot].clear();
            if (!effectShader->emitGradientTexture.get().empty()) {
                part.texturePaths[glowSlot] = effectShader->emitGradientTexture.get();
                part.emissiveEnabled = true;
            }
            if (!effectShader->lightingTexture.get().empty()) {
                part.texturePaths[backlightSlot] = effectShader->lightingTexture.get();
            }
            if (!effectShader->reflectanceTexture.get().empty()) {
                part.texturePaths[specularSlot] = effectShader->reflectanceTexture.get();
                part.specularEnabled = true;
                part.specularStrength = std::max(part.specularStrength, 1.0f);
                if (part.specularColor[0] + part.specularColor[1] + part.specularColor[2] < 0.0001f) {
                    part.specularColor[0] = 1.0f;
                    part.specularColor[1] = 1.0f;
                    part.specularColor[2] = 1.0f;
                }
            }
            part.tintColor[0] = part.emissiveColor[0];
            part.tintColor[1] = part.emissiveColor[1];
            part.tintColor[2] = part.emissiveColor[2];
            part.auxiliaryMapMode = 4.0f;
            part.environmentEnabled = part.environmentEnabled ||
                                      !part.texturePaths[TextureSlotIndex(MeshTextureSlot::Environment)].empty();
        }
        if (part.faceGenPart) {
            part.morphSourcePositions.reserve(positions->size());
            for (const nifly::Vector3& sourcePosition : *positions) {
                part.morphSourcePositions.push_back({
                    sourcePosition.x,
                    sourcePosition.y,
                    sourcePosition.z
                });
            }
        } else {
            part.morphToBindBoneFrames.clear();
        }
        totalVertexCount += part.vertices.size();
        totalTriangleCount += part.indices.size() / 3;
        parts.push_back(std::move(part));
    }

    if (!hasBounds || parts.empty()) {
        logger::error("nifly found no renderable shapes in NIF resource: {}", sourcePath);
        return false;
    }

    logger::info(
        "nifly loaded {}: {} shape(s), {} vertices, {} triangles",
        sourcePath,
        parts.size(),
        totalVertexCount,
        totalTriangleCount);

    mesh->boundMin = minimum;
    mesh->boundMax = maximum;
    const RE::NiPoint3 center = (minimum + maximum) * 0.5f;
    animationCenter = center;
    for (MeshPart& part : parts) {
        part.center -= center;
        for (MeshBoneFrame& bindBoneFrame : part.bindBoneFrames) {
            bindBoneFrame.origin -= center;
        }
        for (MeshVertex& vertex : part.vertices) {
            vertex.position[0] -= center.x;
            vertex.position[1] -= center.y;
            vertex.position[2] -= center.z;
            const float radius = std::sqrt(
                vertex.position[0] * vertex.position[0] +
                vertex.position[1] * vertex.position[1] +
                vertex.position[2] * vertex.position[2]);
            boundingRadius = std::max(boundingRadius, radius);
        }
        if (!part.skinWeights.empty()) {
            part.bindVertices = part.vertices;
        }
        if (part.faceGenPart) {
            part.morphBaseVertices = part.vertices;
            if (part.bindVertices.empty()) {
                part.bindVertices = part.vertices;
            }
        }
    }
    return true;
}

Mesh::Mesh(const char* nifPath, uint32_t width, uint32_t height)
{
    Initialize(width, height);
    if (Load(nifPath)) {
        Fit(RE::NiPoint2{0.0f, 0.0f}, RE::NiPoint2{static_cast<float>(width), static_cast<float>(height)});
    }
}

Mesh::Mesh(
    const char* const* basePaths,
    uint32_t basePathCount,
    const char* const* attachmentPaths,
    uint32_t attachmentPathCount,
    uint32_t width,
    uint32_t height)
{
    Initialize(width, height);

    RE::NiPoint3 minimum{};
    RE::NiPoint3 maximum{};
    bool hasBounds = false;
    std::unordered_map<std::string, MeshBoneFrame> bodyBoneFrames;

    auto appendComponent = [&](const char* path) {
        if (!path || !path[0]) {
            return false;
        }

        Mesh component(path, width, height);
        if (!component.IsValid()) {
            return false;
        }

        if (parts.empty()) {
            animationRotation = component.animationRotation;
        }

        const RE::NiPoint3 componentCenter = (component.mesh->boundMin + component.mesh->boundMax) * 0.5f;
        MeshBoneFrame bodyAlignmentFrame{};
        MeshBoneFrame componentAlignmentFrame{};
        bool hasBoneAlignment = false;
        const bool faceGenComponent = !GetFaceTintPath(component.sourcePath).empty();
        if (faceGenComponent) {
            constexpr std::array<std::string_view, 3> alignmentBones{
                "NPC Spine2 [Spn2]",
                "NPC L Clavicle [LClv]",
                "NPC R Clavicle [RClv]"
            };
            for (const std::string_view boneName : alignmentBones) {
                const std::string key(boneName);
                const auto bodyBone = bodyBoneFrames.find(key);
                const auto faceBone = component.boneFrames.find(key);
                if (bodyBone == bodyBoneFrames.end() || faceBone == component.boneFrames.end()) {
                    continue;
                }

                bodyAlignmentFrame = bodyBone->second;
                componentAlignmentFrame = faceBone->second;
                hasBoneAlignment = true;
                logger::info(
                    "Aligned FaceGen component {} to the complete {} bone frame",
                    component.sourcePath,
                    key);
                break;
            }
        } else {
            for (const auto& [boneName, boneFrame] : component.boneFrames) {
                bodyBoneFrames.try_emplace(boneName, boneFrame);
            }
        }

        for (MeshPart& part : component.parts) {
            part.center += componentCenter;
            for (MeshBoneFrame& bindBoneFrame : part.bindBoneFrames) {
                bindBoneFrame.origin += componentCenter;
            }
            if (hasBoneAlignment) {
                part.center = TransformBetweenBoneFrames(
                    part.center,
                    componentAlignmentFrame,
                    bodyAlignmentFrame,
                    false);
                for (MeshBoneFrame& bindBoneFrame : part.bindBoneFrames) {
                    bindBoneFrame.origin = TransformBetweenBoneFrames(
                        bindBoneFrame.origin,
                        componentAlignmentFrame,
                        bodyAlignmentFrame,
                        false);
                    bindBoneFrame.axisX = TransformBetweenBoneFrames(
                        bindBoneFrame.axisX,
                        componentAlignmentFrame,
                        bodyAlignmentFrame,
                        true);
                    bindBoneFrame.axisY = TransformBetweenBoneFrames(
                        bindBoneFrame.axisY,
                        componentAlignmentFrame,
                        bodyAlignmentFrame,
                        true);
                    bindBoneFrame.axisZ = TransformBetweenBoneFrames(
                        bindBoneFrame.axisZ,
                        componentAlignmentFrame,
                        bodyAlignmentFrame,
                        true);
                }
                part.morphToBindFrame.axisX = TransformBetweenBoneFrames(
                    part.morphToBindFrame.axisX,
                    componentAlignmentFrame,
                    bodyAlignmentFrame,
                    true);
                part.morphToBindFrame.axisY = TransformBetweenBoneFrames(
                    part.morphToBindFrame.axisY,
                    componentAlignmentFrame,
                    bodyAlignmentFrame,
                    true);
                part.morphToBindFrame.axisZ = TransformBetweenBoneFrames(
                    part.morphToBindFrame.axisZ,
                    componentAlignmentFrame,
                    bodyAlignmentFrame,
                    true);
                for (MeshBoneFrame& morphFrame : part.morphToBindBoneFrames) {
                    morphFrame.axisX = TransformBetweenBoneFrames(
                        morphFrame.axisX,
                        componentAlignmentFrame,
                        bodyAlignmentFrame,
                        true);
                    morphFrame.axisY = TransformBetweenBoneFrames(
                        morphFrame.axisY,
                        componentAlignmentFrame,
                        bodyAlignmentFrame,
                        true);
                    morphFrame.axisZ = TransformBetweenBoneFrames(
                        morphFrame.axisZ,
                        componentAlignmentFrame,
                        bodyAlignmentFrame,
                        true);
                }
            }
            for (MeshVertex& vertex : part.vertices) {
                RE::NiPoint3 position{
                    vertex.position[0] + componentCenter.x,
                    vertex.position[1] + componentCenter.y,
                    vertex.position[2] + componentCenter.z
                };
                if (hasBoneAlignment) {
                    position = TransformBetweenBoneFrames(
                        position,
                        componentAlignmentFrame,
                        bodyAlignmentFrame,
                        false);
                    const RE::NiPoint3 normal = NormalizeDirection(TransformBetweenBoneFrames(
                        {vertex.normal[0], vertex.normal[1], vertex.normal[2]},
                        componentAlignmentFrame,
                        bodyAlignmentFrame,
                        true));
                    vertex.normal[0] = normal.x;
                    vertex.normal[1] = normal.y;
                    vertex.normal[2] = normal.z;

                    const RE::NiPoint3 tangent = NormalizeDirection(TransformBetweenBoneFrames(
                        {vertex.tangent[0], vertex.tangent[1], vertex.tangent[2]},
                        componentAlignmentFrame,
                        bodyAlignmentFrame,
                        true));
                    vertex.tangent[0] = tangent.x;
                    vertex.tangent[1] = tangent.y;
                    vertex.tangent[2] = tangent.z;
                }
                vertex.position[0] = position.x;
                vertex.position[1] = position.y;
                vertex.position[2] = position.z;

                if (!hasBounds) {
                    minimum = position;
                    maximum = position;
                    hasBounds = true;
                } else {
                    minimum.x = std::min(minimum.x, position.x);
                    minimum.y = std::min(minimum.y, position.y);
                    minimum.z = std::min(minimum.z, position.z);
                    maximum.x = std::max(maximum.x, position.x);
                    maximum.y = std::max(maximum.y, position.y);
                    maximum.z = std::max(maximum.z, position.z);
                }
            }
            parts.push_back(std::move(part));
        }

        if (!sourcePath.empty()) {
            sourcePath.append("; ");
        }
        sourcePath.append(component.sourcePath);
        return true;
    };

    bool loadedBase = false;
    if (basePaths) {
        for (uint32_t pathIndex = 0; pathIndex < basePathCount; ++pathIndex) {
            if (appendComponent(basePaths[pathIndex])) {
                loadedBase = true;
                break;
            }
        }
    }
    if (!loadedBase) {
        return;
    }

    const bool baseIncludesHeadParts = !GetFaceTintPath(sourcePath).empty();
    if (attachmentPaths && !baseIncludesHeadParts) {
        for (uint32_t pathIndex = 0; pathIndex < attachmentPathCount; ++pathIndex) {
            appendComponent(attachmentPaths[pathIndex]);
        }
    }
    if (!hasBounds || parts.empty()) {
        return;
    }

    mesh->boundMin = minimum;
    mesh->boundMax = maximum;
    const RE::NiPoint3 center = (minimum + maximum) * 0.5f;
    animationCenter = center;
    for (MeshPart& part : parts) {
        part.center -= center;
        for (MeshBoneFrame& bindBoneFrame : part.bindBoneFrames) {
            bindBoneFrame.origin -= center;
        }
        for (MeshVertex& vertex : part.vertices) {
            vertex.position[0] -= center.x;
            vertex.position[1] -= center.y;
            vertex.position[2] -= center.z;
            const float radius = std::sqrt(
                vertex.position[0] * vertex.position[0] +
                vertex.position[1] * vertex.position[1] +
                vertex.position[2] * vertex.position[2]);
            boundingRadius = std::max(boundingRadius, radius);
        }
        if (!part.skinWeights.empty()) {
            part.bindVertices = part.vertices;
        }
        if (part.faceGenPart) {
            part.morphBaseVertices = part.vertices;
            if (part.bindVertices.empty()) {
                part.bindVertices = part.vertices;
            }
        }
    }

    logger::info("nifly assembled composite mesh from {}", sourcePath);
    Fit(RE::NiPoint2{0.0f, 0.0f}, RE::NiPoint2{static_cast<float>(width), static_cast<float>(height)});
}

Mesh::~Mesh()
{
    if (mesh) {
        ReleaseResource(mesh->SRV);
        ReleaseResource(mesh->texture);
        mesh->savePath = nullptr;
    }
    delete mesh;
}

bool Mesh::IsValid() const
{
    return mesh && !parts.empty();
}

bool Mesh::SetBoneLocalPose(
    const char* const* boneNames,
    const std::int16_t* parentIndices,
    const MeshRenderingFrameworkAPI::BoneTransform* transforms,
    uint32_t transformCount)
{
    if (!mesh || !boneNames || !parentIndices || !transforms || transformCount == 0) {
        return false;
    }

    std::vector<MeshBoneFrame> globalFrames(transformCount);
    std::unordered_map<std::string, MeshBoneFrame> animatedBoneFrames;
    animatedBoneFrames.reserve(transformCount);
    for (std::uint32_t boneIndex = 0; boneIndex < transformCount; ++boneIndex) {
        MeshBoneFrame globalFrame = CreateBoneFrame(transforms[boneIndex]);
        const std::int16_t parentIndex = parentIndices[boneIndex];
        if (parentIndex >= 0 && static_cast<std::uint32_t>(parentIndex) < boneIndex) {
            globalFrame = ComposeBoneFrames(globalFrames[parentIndex], globalFrame);
        }
        globalFrames[boneIndex] = globalFrame;

        if (!boneNames[boneIndex] || !boneNames[boneIndex][0]) {
            continue;
        }
        MeshBoneFrame renderFrame = RotateBoneFrame(animationRotation, globalFrame);
        renderFrame.origin -= animationCenter;
        animatedBoneFrames.insert_or_assign(boneNames[boneIndex], renderFrame);
    }

    constexpr std::string_view headBoneName = "NPC Head [Head]";
    bool updated = false;
    for (MeshPart& part : parts) {
        if (part.bindVertices.size() != part.vertices.size() ||
            part.skinBoneNames.size() != part.bindBoneFrames.size() ||
            part.skinWeights.empty()) {
            continue;
        }

        const MeshBoneFrame* fallbackBindFrame = nullptr;
        const MeshBoneFrame* fallbackAnimatedFrame = nullptr;
        const auto animatedHead = animatedBoneFrames.find(std::string(headBoneName));
        if (animatedHead != animatedBoneFrames.end()) {
            for (std::size_t boneIndex = 0; boneIndex < part.skinBoneNames.size(); ++boneIndex) {
                if (part.skinBoneNames[boneIndex] == headBoneName) {
                    fallbackBindFrame = &part.bindBoneFrames[boneIndex];
                    fallbackAnimatedFrame = &animatedHead->second;
                    break;
                }
            }
        }

        part.vertices = part.bindVertices;
        std::vector<RE::NiPoint3> positionSums(part.vertices.size());
        std::vector<RE::NiPoint3> normalSums(part.vertices.size());
        std::vector<RE::NiPoint3> tangentSums(part.vertices.size());
        std::vector<float> weightSums(part.vertices.size(), 0.0f);

        for (const MeshSkinWeight& skinWeight : part.skinWeights) {
            if (skinWeight.vertexIndex >= part.bindVertices.size() ||
                skinWeight.boneIndex >= part.skinBoneNames.size() ||
                skinWeight.weight <= 0.0f) {
                continue;
            }

            const MeshVertex& bindVertex = part.bindVertices[skinWeight.vertexIndex];
            const RE::NiPoint3 bindPosition{
                bindVertex.position[0],
                bindVertex.position[1],
                bindVertex.position[2]
            };
            const RE::NiPoint3 bindNormal{
                bindVertex.normal[0],
                bindVertex.normal[1],
                bindVertex.normal[2]
            };
            const RE::NiPoint3 bindTangent{
                bindVertex.tangent[0],
                bindVertex.tangent[1],
                bindVertex.tangent[2]
            };

            const MeshBoneFrame* sourceFrame = nullptr;
            const MeshBoneFrame* targetFrame = nullptr;
            const auto animatedBone = animatedBoneFrames.find(part.skinBoneNames[skinWeight.boneIndex]);
            if (animatedBone != animatedBoneFrames.end()) {
                sourceFrame = &part.bindBoneFrames[skinWeight.boneIndex];
                targetFrame = &animatedBone->second;
            } else if (fallbackBindFrame && fallbackAnimatedFrame) {
                sourceFrame = fallbackBindFrame;
                targetFrame = fallbackAnimatedFrame;
            }

            const RE::NiPoint3 posedPosition = sourceFrame && targetFrame
                ? TransformBetweenBoneFrames(bindPosition, *sourceFrame, *targetFrame, false)
                : bindPosition;
            const RE::NiPoint3 posedNormal = sourceFrame && targetFrame
                ? TransformBetweenBoneFrames(bindNormal, *sourceFrame, *targetFrame, true)
                : bindNormal;
            const RE::NiPoint3 posedTangent = sourceFrame && targetFrame
                ? TransformBetweenBoneFrames(bindTangent, *sourceFrame, *targetFrame, true)
                : bindTangent;
            positionSums[skinWeight.vertexIndex] += posedPosition * skinWeight.weight;
            normalSums[skinWeight.vertexIndex] += posedNormal * skinWeight.weight;
            tangentSums[skinWeight.vertexIndex] += posedTangent * skinWeight.weight;
            weightSums[skinWeight.vertexIndex] += skinWeight.weight;
        }

        RE::NiPoint3 partMinimum{
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()
        };
        RE::NiPoint3 partMaximum{
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()
        };
        for (std::size_t vertexIndex = 0; vertexIndex < part.vertices.size(); ++vertexIndex) {
            MeshVertex& vertex = part.vertices[vertexIndex];
            const float weight = weightSums[vertexIndex];
            if (weight > 0.00001f) {
                const RE::NiPoint3 position = positionSums[vertexIndex] / weight;
                const RE::NiPoint3 normal = NormalizeDirection(normalSums[vertexIndex] / weight);
                const RE::NiPoint3 weightedTangent = tangentSums[vertexIndex] / weight;
                const RE::NiPoint3 tangent = NormalizeDirection(
                    weightedTangent - normal * DotProduct(normal, weightedTangent));
                vertex.position[0] = position.x;
                vertex.position[1] = position.y;
                vertex.position[2] = position.z;
                vertex.normal[0] = normal.x;
                vertex.normal[1] = normal.y;
                vertex.normal[2] = normal.z;
                vertex.tangent[0] = tangent.x;
                vertex.tangent[1] = tangent.y;
                vertex.tangent[2] = tangent.z;
            }
            partMinimum.x = std::min(partMinimum.x, vertex.position[0]);
            partMinimum.y = std::min(partMinimum.y, vertex.position[1]);
            partMinimum.z = std::min(partMinimum.z, vertex.position[2]);
            partMaximum.x = std::max(partMaximum.x, vertex.position[0]);
            partMaximum.y = std::max(partMaximum.y, vertex.position[1]);
            partMaximum.z = std::max(partMaximum.z, vertex.position[2]);
        }
        part.center = (partMinimum + partMaximum) * 0.5f;
        part.vertexBufferDirty = true;
        updated = true;
    }

    if (updated) {
        mesh->mustUpdate = true;
    }
    return updated;
}

bool Mesh::PlayAnimation(const char* animationPath, const char* skeletonPath, bool loop)
{
    std::unique_ptr<GameAnimation> loadedAnimation = std::make_unique<GameAnimation>();
    if (!loadedAnimation->Load(animationPath, skeletonPath, loop)) {
        return false;
    }
    animation = std::move(loadedAnimation);
    mesh->alwaysUpdate = true;
    mesh->mustUpdate = true;
    return true;
}

bool Mesh::ApplyMorphDeltas(
    MeshPart& part,
    const std::vector<RE::NiPoint3>& sourceDeltas)
{
    if (!mesh ||
        part.morphBaseVertices.empty() ||
        part.morphBaseVertices.size() != part.vertices.size() ||
        sourceDeltas.size() != part.morphBaseVertices.size()) {
        return false;
    }

    part.bindVertices = part.morphBaseVertices;
    if (!part.skinWeights.empty() &&
        part.morphToBindBoneFrames.size() == part.skinBoneNames.size()) {
        std::vector<RE::NiPoint3> deltaSums(part.vertices.size());
        std::vector<float> weightSums(part.vertices.size(), 0.0f);
        for (const MeshSkinWeight& skinWeight : part.skinWeights) {
            if (skinWeight.vertexIndex >= sourceDeltas.size() ||
                skinWeight.boneIndex >= part.morphToBindBoneFrames.size() ||
                skinWeight.weight <= 0.0f) {
                continue;
            }
            const RE::NiPoint3 transformedDelta = ApplyBoneFrame(
                part.morphToBindBoneFrames[skinWeight.boneIndex],
                sourceDeltas[skinWeight.vertexIndex],
                true);
            deltaSums[skinWeight.vertexIndex] += transformedDelta * skinWeight.weight;
            weightSums[skinWeight.vertexIndex] += skinWeight.weight;
        }
        for (std::size_t vertexIndex = 0; vertexIndex < part.bindVertices.size(); ++vertexIndex) {
            const float weight = weightSums[vertexIndex];
            if (weight <= 0.00001f) {
                continue;
            }
            const RE::NiPoint3 delta = deltaSums[vertexIndex] / weight;
            MeshVertex& vertex = part.bindVertices[vertexIndex];
            vertex.position[0] += delta.x;
            vertex.position[1] += delta.y;
            vertex.position[2] += delta.z;
        }
    } else {
        for (std::size_t vertexIndex = 0; vertexIndex < part.bindVertices.size(); ++vertexIndex) {
            const RE::NiPoint3 delta = ApplyBoneFrame(
                part.morphToBindFrame,
                sourceDeltas[vertexIndex],
                true);
            MeshVertex& vertex = part.bindVertices[vertexIndex];
            vertex.position[0] += delta.x;
            vertex.position[1] += delta.y;
            vertex.position[2] += delta.z;
        }
    }

    part.vertices = part.bindVertices;
    RE::NiPoint3 minimum{
        part.vertices.front().position[0],
        part.vertices.front().position[1],
        part.vertices.front().position[2]
    };
    RE::NiPoint3 maximum = minimum;
    for (const MeshVertex& vertex : part.vertices) {
        minimum.x = std::min(minimum.x, vertex.position[0]);
        minimum.y = std::min(minimum.y, vertex.position[1]);
        minimum.z = std::min(minimum.z, vertex.position[2]);
        maximum.x = std::max(maximum.x, vertex.position[0]);
        maximum.y = std::max(maximum.y, vertex.position[1]);
        maximum.z = std::max(maximum.z, vertex.position[2]);
    }
    part.center = (minimum + maximum) * 0.5f;
    part.vertexBufferDirty = true;
    mesh->mustUpdate = true;
    return true;
}

bool Mesh::SetMorph(const char* triPath, const char* morphName, float value)
{
    if (!mesh || !triPath || !triPath[0] || !morphName || !morphName[0] || !std::isfinite(value)) {
        return false;
    }

    TriMorph morph;
    if (!ReadTriMorph(triPath, morphName, morph)) {
        return false;
    }

    MeshPart* matchingPart = nullptr;
    double bestError = std::numeric_limits<double>::max();
    for (MeshPart& part : parts) {
        if (!part.faceGenPart ||
            part.morphSourcePositions.size() != morph.basePositions.size() ||
            part.morphBaseVertices.size() != morph.deltas.size()) {
            continue;
        }

        double error = 0.0;
        for (std::size_t vertexIndex = 0; vertexIndex < morph.basePositions.size(); ++vertexIndex) {
            const RE::NiPoint3 difference =
                part.morphSourcePositions[vertexIndex] - morph.basePositions[vertexIndex];
            error += static_cast<double>(difference.x) * difference.x;
            error += static_cast<double>(difference.y) * difference.y;
            error += static_cast<double>(difference.z) * difference.z;
        }
        if (error < bestError) {
            bestError = error;
            matchingPart = &part;
        }
    }
    if (!matchingPart) {
        logger::warn("Could not match TRI morph {} from {} to a FaceGen mesh part", morphName, triPath);
        return false;
    }

    const float morphValue = std::clamp(value, 0.0f, 1.0f);
    for (RE::NiPoint3& delta : morph.deltas) {
        delta *= morphValue;
    }
    faceMorphActor.reset();
    const bool applied = ApplyMorphDeltas(*matchingPart, morph.deltas);
    if (applied) {
        logger::info("Applied TRI morph {} from {} to {}", morphName, triPath, matchingPart->shapeName);
    }
    return applied;
}

bool Mesh::ClearFaceMorphs()
{
    if (!mesh) {
        return false;
    }

    faceMorphActor.reset();
    bool cleared = false;
    for (MeshPart& part : parts) {
        if (!part.faceGenPart || part.morphBaseVertices.size() != part.vertices.size()) {
            continue;
        }
        const std::vector<RE::NiPoint3> zeroDeltas(part.morphBaseVertices.size());
        cleared = ApplyMorphDeltas(part, zeroDeltas) || cleared;
    }
    return cleared;
}

bool Mesh::SetFaceMorphSource(RE::Actor* actor)
{
    if (!mesh || !actor) {
        return false;
    }

    faceMorphActor = actor->GetHandle();
    mesh->alwaysUpdate = true;
    mesh->mustUpdate = true;
    UpdateFaceMorphs();
    return true;
}

bool Mesh::SetTextureSet(
    const char* nifPath,
    const char* const* texturePaths,
    std::uint32_t texturePathCount,
    bool modelSpaceNormals,
    bool includeBodyShape)
{
    if (!mesh || !nifPath || !nifPath[0] || !texturePaths) {
        return false;
    }

    const std::string targetPath = Lowercase(NormalizeNifPath(nifPath));
    bool updated = false;
    for (MeshPart& part : parts) {
        const bool bodyShape = includeBodyShape && part.shapeName == "body";
        if (Lowercase(part.sourcePath) != targetPath ||
            (!bodyShape && !part.faceOrSkin)) {
            continue;
        }

        if (bodyShape) {
            part.skinTinted = true;
            part.faceOrSkin = true;
            part.auxiliaryMapMode = 2.0f;
        }

        const auto applyTexture = [&](std::uint32_t sourceIndex, MeshTextureSlot destinationSlot) {
            if (sourceIndex >= texturePathCount) {
                return;
            }

            const std::size_t destinationIndex = TextureSlotIndex(destinationSlot);
            const char* texturePath = texturePaths[sourceIndex];
            part.texturePaths[destinationIndex] = texturePath ? texturePath : "";
            ReleaseResource(part.textureViews[destinationIndex]);
            part.textureLoadAttempted[destinationIndex] = false;
        };

        applyTexture(0, MeshTextureSlot::Diffuse);
        applyTexture(1, MeshTextureSlot::Normal);
        // Armor-addon TX02 is the skin subsurface/tint texture. Skin shaders
        // consume it through the renderer's auxiliary texture slot.
        if (texturePathCount > 2 && texturePaths[2] && texturePaths[2][0]) {
            applyTexture(2, MeshTextureSlot::Glow);
        } else {
            applyTexture(3, MeshTextureSlot::Glow);
        }
        applyTexture(4, MeshTextureSlot::Height);
        applyTexture(5, MeshTextureSlot::Environment);
        const bool usesModelSpaceNormals = part.modelSpaceNormals || modelSpaceNormals;
        const bool hasLightingMapOverride =
            texturePathCount > 7 &&
            texturePaths[7] &&
            texturePaths[7][0];
        if (hasLightingMapOverride) {
            applyTexture(
                7,
                usesModelSpaceNormals ? MeshTextureSlot::Specular : MeshTextureSlot::Backlight);
        }
        const bool hasSeparateSpecularMap =
            usesModelSpaceNormals &&
            !part.texturePaths[TextureSlotIndex(MeshTextureSlot::Specular)].empty();
        if (hasSeparateSpecularMap) {
            part.specularEnabled = true;
            part.specularStrength = std::max(part.specularStrength, 0.5f);
            if (part.specularColor[0] + part.specularColor[1] + part.specularColor[2] < 0.0001f) {
                part.specularColor[0] = 1.0f;
                part.specularColor[1] = 1.0f;
                part.specularColor[2] = 1.0f;
            }
        }
        const std::size_t unusedLightingMapIndex = TextureSlotIndex(
            usesModelSpaceNormals ? MeshTextureSlot::Backlight : MeshTextureSlot::Specular);
        part.texturePaths[unusedLightingMapIndex].clear();
        ReleaseResource(part.textureViews[unusedLightingMapIndex]);
        part.textureLoadAttempted[unusedLightingMapIndex] = false;

        part.modelSpaceNormals = usesModelSpaceNormals;
        part.environmentEnabled =
            !part.texturePaths[TextureSlotIndex(MeshTextureSlot::Environment)].empty();
        updated = true;
    }

    if (updated) {
        mesh->mustUpdate = true;
    }
    return updated;
}

bool Mesh::UpdateFaceMorphs()
{
    if (!faceMorphActor) {
        return false;
    }

    RE::NiPointer<RE::Actor> actor = faceMorphActor.get();
    if (!actor) {
        return false;
    }
    RE::BSFaceGenNiNode* faceNode = actor->GetFaceNodeSkinned();
    if (!faceNode) {
        return false;
    }

    std::vector<LiveFaceShape> liveShapes;
    CollectLiveFaceShapes(faceNode, liveShapes);
    if (liveShapes.empty()) {
        return false;
    }

    bool updated = false;
    for (MeshPart& part : parts) {
        if (!part.faceGenPart ||
            part.morphBaseVertices.size() != part.vertices.size() ||
            part.morphSourcePositions.size() != part.vertices.size()) {
            continue;
        }

        LiveFaceShape* matchingShape = nullptr;
        for (LiveFaceShape& liveShape : liveShapes) {
            if (!liveShape.used &&
                liveShape.positions.size() == part.morphSourcePositions.size() &&
                liveShape.name == part.shapeName) {
                matchingShape = &liveShape;
                break;
            }
        }
        if (!matchingShape) {
            LiveFaceShape* countMatch = nullptr;
            for (LiveFaceShape& liveShape : liveShapes) {
                if (liveShape.used ||
                    liveShape.positions.size() != part.morphSourcePositions.size()) {
                    continue;
                }
                if (countMatch) {
                    countMatch = nullptr;
                    break;
                }
                countMatch = &liveShape;
            }
            matchingShape = countMatch;
        }
        if (!matchingShape) {
            continue;
        }
        matchingShape->used = true;

        std::vector<RE::NiPoint3> sourceDeltas(part.vertices.size());
        for (std::size_t vertexIndex = 0; vertexIndex < sourceDeltas.size(); ++vertexIndex) {
            sourceDeltas[vertexIndex] =
                matchingShape->positions[vertexIndex] - part.morphSourcePositions[vertexIndex];
        }

        updated = ApplyMorphDeltas(part, sourceDeltas) || updated;
    }
    return updated;
}

bool Mesh::UpdateAnimation()
{
    bool updated = UpdateFaceMorphs();
    if (!animation) {
        return updated;
    }
    std::vector<MeshRenderingFrameworkAPI::BoneTransform> pose;
    if (!animation->Sample(pose) || pose.empty()) {
        return updated;
    }
    const std::vector<const char*>& names = animation->GetBoneNames();
    const std::vector<std::int16_t>& parents = animation->GetParentIndices();
    if (names.size() != pose.size() || parents.size() != pose.size()) {
        return updated;
    }
    return SetBoneLocalPose(
        names.data(),
        parents.data(),
        pose.data(),
        static_cast<std::uint32_t>(pose.size())) || updated;
}

bool Mesh::InitializeGpuResources(ID3D11Device* device)
{
    if (!device || !IsValid()) {
        return false;
    }

    for (MeshPart& part : parts) {
        if (!part.vertexBuffer) {
            D3D11_BUFFER_DESC vertexDescription{};
            vertexDescription.ByteWidth = static_cast<UINT>(part.vertices.size() * sizeof(MeshVertex));
            const bool animated = !part.skinWeights.empty() || !part.morphBaseVertices.empty();
            vertexDescription.Usage = animated ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_IMMUTABLE;
            vertexDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            vertexDescription.CPUAccessFlags = animated ? D3D11_CPU_ACCESS_WRITE : 0;
            D3D11_SUBRESOURCE_DATA vertexData{};
            vertexData.pSysMem = part.vertices.data();
            const HRESULT vertexResult = device->CreateBuffer(&vertexDescription, &vertexData, &part.vertexBuffer);
            if (FAILED(vertexResult)) {
                logger::error(
                    "Failed to create nifly vertex buffer for {} vertices: {:08X}",
                    part.vertices.size(),
                    static_cast<std::uint32_t>(vertexResult));
                ResetGpuResources();
                return false;
            }
            part.vertexBufferDirty = false;
        }

        if (!part.indexBuffer) {
            D3D11_BUFFER_DESC indexDescription{};
            indexDescription.ByteWidth = static_cast<UINT>(part.indices.size() * sizeof(std::uint16_t));
            indexDescription.Usage = D3D11_USAGE_IMMUTABLE;
            indexDescription.BindFlags = D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA indexData{};
            indexData.pSysMem = part.indices.data();
            const HRESULT indexResult = device->CreateBuffer(&indexDescription, &indexData, &part.indexBuffer);
            if (FAILED(indexResult)) {
                logger::error(
                    "Failed to create nifly index buffer for {} indices: {:08X}",
                    part.indices.size(),
                    static_cast<std::uint32_t>(indexResult));
                ResetGpuResources();
                return false;
            }
        }

        for (std::size_t textureIndex = 0; textureIndex < MeshTextureSlotCount; ++textureIndex) {
            if (part.textureLoadAttempted[textureIndex] || part.texturePaths[textureIndex].empty()) {
                continue;
            }

            part.textureLoadAttempted[textureIndex] = true;
            const MeshTextureSlot textureSlot = static_cast<MeshTextureSlot>(textureIndex);
            const bool colorTexture = textureSlot == MeshTextureSlot::Diffuse ||
                                      textureSlot == MeshTextureSlot::Glow ||
                                      textureSlot == MeshTextureSlot::Environment ||
                                      textureSlot == MeshTextureSlot::Subsurface ||
                                      textureSlot == MeshTextureSlot::Backlight ||
                                      textureSlot == MeshTextureSlot::FaceTint;
            part.textureViews[textureIndex] = LoadTexture(
                device,
                part.texturePaths[textureIndex],
                colorTexture);
            if (!part.textureViews[textureIndex]) {
                const std::string resolvedTexturePath = NormalizeTexturePath(part.texturePaths[textureIndex]);
                logger::warn(
                    "Could not load NIF texture {} (resolved as {}) for {}",
                    part.texturePaths[textureIndex],
                    resolvedTexturePath,
                    sourcePath);
            }
        }
    }
    return true;
}

void Mesh::ResetGpuResources()
{
    for (MeshPart& part : parts) {
        part.ResetGpuResources();
    }
    if (mesh) {
        ReleaseResource(mesh->SRV);
        ReleaseResource(mesh->texture);
        mesh->mustUpdate = true;
    }
}

void Mesh::Draw(
    ID3D11DeviceContext* context,
    ID3D11ShaderResourceView* const* fallbackTextures,
    ID3D11Buffer* materialConstantBuffer,
    bool transparentPass)
{
    if (!context || !fallbackTextures || !materialConstantBuffer) {
        return;
    }

    constexpr UINT stride = sizeof(MeshVertex);
    constexpr UINT offset = 0;

    std::vector<MeshPart*> drawParts;
    drawParts.reserve(parts.size());
    for (MeshPart& part : parts) {
        const bool transparent = part.alphaMode == MeshAlphaMode::Blend;
        if (transparent == transparentPass) {
            drawParts.push_back(&part);
        }
    }

    if (transparentPass) {
        std::sort(drawParts.begin(), drawParts.end(), [this](const MeshPart* left, const MeshPart* right) {
            const RE::NiPoint3 leftCenter = mesh->rotation * (left->center * mesh->scale) + mesh->position;
            const RE::NiPoint3 rightCenter = mesh->rotation * (right->center * mesh->scale) + mesh->position;
            return leftCenter.y < rightCenter.y;
        });
    }

    for (MeshPart* partPointer : drawParts) {
        MeshPart& part = *partPointer;
        if (!part.vertexBuffer || !part.indexBuffer || part.indices.empty()) {
            continue;
        }

        if (part.vertexBufferDirty) {
            D3D11_MAPPED_SUBRESOURCE mappedVertexBuffer{};
            if (SUCCEEDED(context->Map(
                    part.vertexBuffer,
                    0,
                    D3D11_MAP_WRITE_DISCARD,
                    0,
                    &mappedVertexBuffer))) {
                std::memcpy(
                    mappedVertexBuffer.pData,
                    part.vertices.data(),
                    part.vertices.size() * sizeof(MeshVertex));
                context->Unmap(part.vertexBuffer, 0);
                part.vertexBufferDirty = false;
            }
        }

        MeshMaterialConstants materialConstants{};
        materialConstants.alphaThreshold = part.alphaThreshold;
        materialConstants.materialAlpha = part.materialAlpha;
        materialConstants.blendEnabled = transparentPass ? 1.0f : 0.0f;
        materialConstants.specularStrength = part.specularStrength;
        std::copy(std::begin(part.specularColor), std::end(part.specularColor), materialConstants.specularColor);
        materialConstants.glossiness = part.glossiness;
        std::copy(std::begin(part.emissiveColor), std::end(part.emissiveColor), materialConstants.emissiveColor);
        materialConstants.emissiveMultiple = part.emissiveMultiple;
        std::copy(std::begin(part.tintColor), std::end(part.tintColor), materialConstants.tintColor);
        materialConstants.environmentScale = part.environmentScale;
        std::copy(std::begin(part.uvScale), std::end(part.uvScale), materialConstants.uvScale);
        std::copy(std::begin(part.uvOffset), std::end(part.uvOffset), materialConstants.uvOffset);
        materialConstants.parallaxScale = part.parallaxScale;
        materialConstants.backlightPower = part.backlightPower;
        materialConstants.rimlightPower = part.rimlightPower;
        materialConstants.auxiliaryMapMode = part.auxiliaryMapMode;
        materialConstants.hasNormalMap = part.textureViews[TextureSlotIndex(MeshTextureSlot::Normal)] ? 1.0f : 0.0f;
        materialConstants.hasHeightMap = part.textureViews[TextureSlotIndex(MeshTextureSlot::Height)] ? 1.0f : 0.0f;
        materialConstants.hasEnvironmentMap =
            part.environmentEnabled && part.textureViews[TextureSlotIndex(MeshTextureSlot::Environment)] ? 1.0f : 0.0f;
        materialConstants.hasEnvironmentMask =
            part.textureViews[TextureSlotIndex(MeshTextureSlot::EnvironmentMask)] ? 1.0f : 0.0f;
        materialConstants.hasSubsurfaceMap =
            part.textureViews[TextureSlotIndex(MeshTextureSlot::Subsurface)] ? 1.0f : 0.0f;
        materialConstants.hasBacklightMap =
            part.textureViews[TextureSlotIndex(MeshTextureSlot::Backlight)] ? 1.0f : 0.0f;
        materialConstants.hasSpecularMap =
            part.textureViews[TextureSlotIndex(MeshTextureSlot::Specular)] ? 1.0f : 0.0f;
        materialConstants.modelSpaceNormals = part.modelSpaceNormals ? 1.0f : 0.0f;
        materialConstants.useVertexColors = part.useVertexColors ? 1.0f : 0.0f;
        materialConstants.useVertexAlpha = part.useVertexAlpha ? 1.0f : 0.0f;
        materialConstants.emissiveEnabled = part.emissiveEnabled ? 1.0f : 0.0f;
        materialConstants.specularEnabled = part.specularEnabled ? 1.0f : 0.0f;
        materialConstants.hasAuxiliaryMap =
            part.textureViews[TextureSlotIndex(MeshTextureSlot::Glow)] ? 1.0f : 0.0f;
        const bool hasFaceTintMap =
            part.textureViews[TextureSlotIndex(MeshTextureSlot::FaceTint)] != nullptr;
        materialConstants.hasFaceTintMap = hasFaceTintMap ? 1.0f : 0.0f;
        materialConstants.faceOrSkin = part.faceOrSkin ? 1.0f : 0.0f;
        materialConstants.hairMaterial = part.hairMaterial ? 1.0f : 0.0f;
        materialConstants.skinTinted = part.skinTinted ? 1.0f : 0.0f;
        materialConstants.useBodyTint =
            mesh->useBodyTint &&
                    (part.skinTinted || (part.faceOrSkin && !hasFaceTintMap))
                ? 1.0f
                : 0.0f;
        std::copy(
            std::begin(mesh->bodyTintColor),
            std::end(mesh->bodyTintColor),
            materialConstants.bodyTintColor);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(materialConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            continue;
        }
        std::memcpy(mapped.pData, &materialConstants, sizeof(materialConstants));
        context->Unmap(materialConstantBuffer, 0);

        ID3D11Buffer* vertexBuffer = part.vertexBuffer;
        context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
        context->IASetIndexBuffer(part.indexBuffer, DXGI_FORMAT_R16_UINT, 0);

        std::array<ID3D11ShaderResourceView*, MeshTextureSlotCount> textureViews{};
        for (std::size_t textureIndex = 0; textureIndex < MeshTextureSlotCount; ++textureIndex) {
            textureViews[textureIndex] = part.textureViews[textureIndex]
                ? part.textureViews[textureIndex]
                : fallbackTextures[textureIndex];
        }
        context->PSSetShaderResources(0, static_cast<UINT>(textureViews.size()), textureViews.data());
        context->DrawIndexed(static_cast<UINT>(part.indices.size()), 0, 0);
    }
}
