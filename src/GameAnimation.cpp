#include "GameAnimation.h"

#include "RE/H/hkaSplineCompressedAnimation.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <utility>

namespace
{
    constexpr std::uint32_t HkxMagic = 0x57E0E057;
    constexpr std::size_t SectionHeaderStart = 0x40;
    constexpr std::size_t SectionHeaderSize = 0x30;
    constexpr std::uint32_t DontDeallocateFlag = 0x80000000u;

    struct HkxSection
    {
        std::uint32_t offset = 0;
        std::uint32_t localFixups = 0;
        std::uint32_t globalFixups = 0;
        std::uint32_t virtualFixups = 0;
        std::uint32_t exports = 0;
        std::uint32_t end = 0;
    };

    struct HkxDocument
    {
        const std::vector<std::uint8_t>& bytes;
        HkxSection classNames;
        HkxSection data;
        std::unordered_map<std::uint32_t, std::uint32_t> localFixups;
        std::vector<std::pair<std::uint32_t, std::string>> objects;
        std::uint8_t pointerSize = 0;
    };

    template <class T>
    bool ReadValue(const std::vector<std::uint8_t>& bytes, std::size_t offset, T& value)
    {
        if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
            return false;
        }
        std::memcpy(&value, bytes.data() + offset, sizeof(T));
        return true;
    }

    template <class T>
    void WriteValue(std::array<std::byte, 0xB0>& bytes, std::size_t offset, const T& value)
    {
        if (offset <= bytes.size() && sizeof(T) <= bytes.size() - offset) {
            std::memcpy(bytes.data() + offset, &value, sizeof(T));
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

    bool ReadSection(
        const std::vector<std::uint8_t>& bytes,
        std::size_t sectionIndex,
        std::string& name,
        HkxSection& section)
    {
        const std::size_t base = SectionHeaderStart + sectionIndex * SectionHeaderSize;
        if (base > bytes.size() || SectionHeaderSize > bytes.size() - base) {
            return false;
        }
        const char* nameData = reinterpret_cast<const char*>(bytes.data() + base);
        const std::size_t nameLength = std::find(nameData, nameData + 16, '\0') - nameData;
        name.assign(nameData, nameLength);

        std::uint32_t localOffset = 0;
        std::uint32_t globalOffset = 0;
        std::uint32_t virtualOffset = 0;
        std::uint32_t exportOffset = 0;
        std::uint32_t endOffset = 0;
        if (!ReadValue(bytes, base + 0x14, section.offset) ||
            !ReadValue(bytes, base + 0x18, localOffset) ||
            !ReadValue(bytes, base + 0x1C, globalOffset) ||
            !ReadValue(bytes, base + 0x20, virtualOffset) ||
            !ReadValue(bytes, base + 0x24, exportOffset) ||
            !ReadValue(bytes, base + 0x2C, endOffset)) {
            return false;
        }
        section.localFixups = section.offset + localOffset;
        section.globalFixups = section.offset + globalOffset;
        section.virtualFixups = section.offset + virtualOffset;
        section.exports = section.offset + exportOffset;
        section.end = section.offset + endOffset;
        return section.offset <= bytes.size() && section.end <= bytes.size();
    }

    bool ParseDocument(const std::vector<std::uint8_t>& bytes, HkxDocument& document)
    {
        std::uint32_t magic = 0;
        if (!ReadValue(bytes, 0, magic) || magic != HkxMagic || bytes.size() <= 0x10) {
            return false;
        }
        document.pointerSize = bytes[0x10];
        if (document.pointerSize != 8) {
            logger::error("Only Skyrim SE/AE 64-bit HKX resources are supported");
            return false;
        }

        bool foundClassNames = false;
        bool foundData = false;
        for (std::size_t sectionIndex = 0; sectionIndex < 3; ++sectionIndex) {
            std::string name;
            HkxSection section;
            if (!ReadSection(bytes, sectionIndex, name, section)) {
                return false;
            }
            if (name == "__classnames__") {
                document.classNames = section;
                foundClassNames = true;
            } else if (name == "__data__") {
                document.data = section;
                foundData = true;
            }
        }
        if (!foundClassNames || !foundData ||
            document.data.localFixups > document.data.globalFixups ||
            document.data.virtualFixups > document.data.exports) {
            return false;
        }

        for (std::size_t offset = document.data.localFixups;
             offset + 8 <= document.data.globalFixups;
             offset += 8) {
            std::uint32_t source = 0;
            std::uint32_t target = 0;
            if (!ReadValue(bytes, offset, source) || !ReadValue(bytes, offset + 4, target)) {
                return false;
            }
            if (source == std::numeric_limits<std::uint32_t>::max()) {
                break;
            }
            document.localFixups.insert_or_assign(source, target);
        }

        for (std::size_t offset = document.data.virtualFixups;
             offset + 12 <= document.data.exports;
             offset += 12) {
            std::uint32_t objectOffset = 0;
            std::uint32_t classNameOffset = 0;
            if (!ReadValue(bytes, offset, objectOffset) ||
                !ReadValue(bytes, offset + 8, classNameOffset)) {
                return false;
            }
            if (objectOffset == std::numeric_limits<std::uint32_t>::max()) {
                break;
            }
            const std::size_t classNameStart = document.classNames.offset + classNameOffset;
            if (classNameStart >= bytes.size()) {
                return false;
            }
            const std::uint8_t* begin = bytes.data() + classNameStart;
            const std::uint8_t* end = std::find(begin, bytes.data() + bytes.size(), std::uint8_t{0});
            if (end == bytes.data() + bytes.size()) {
                return false;
            }
            document.objects.emplace_back(
                objectOffset,
                reinterpret_cast<const char*>(begin));
        }
        return true;
    }

    bool FindObject(const HkxDocument& document, const char* className, std::uint32_t& objectOffset)
    {
        for (const auto& [offset, name] : document.objects) {
            if (name == className) {
                objectOffset = offset;
                return true;
            }
        }
        return false;
    }

    bool ReadString(const HkxDocument& document, std::uint32_t pointerOffset, std::string& value)
    {
        const auto target = document.localFixups.find(pointerOffset);
        if (target == document.localFixups.end()) {
            return false;
        }
        const std::size_t absoluteOffset = document.data.offset + target->second;
        if (absoluteOffset >= document.bytes.size()) {
            return false;
        }
        const std::uint8_t* begin = document.bytes.data() + absoluteOffset;
        const std::uint8_t* end = std::find(
            begin,
            document.bytes.data() + document.bytes.size(),
            std::uint8_t{0});
        if (end == document.bytes.data() + document.bytes.size()) {
            return false;
        }
        value.assign(reinterpret_cast<const char*>(begin), end - begin);
        return true;
    }

    template <class T>
    bool ReadArray(
        const HkxDocument& document,
        std::uint32_t objectOffset,
        std::uint32_t fieldOffset,
        std::vector<T>& values)
    {
        const std::uint32_t arrayOffset = objectOffset + fieldOffset;
        std::uint32_t count = 0;
        if (!ReadValue(
                document.bytes,
                document.data.offset + arrayOffset + document.pointerSize,
                count)) {
            return false;
        }
        if (count == 0) {
            values.clear();
            return true;
        }
        const auto target = document.localFixups.find(arrayOffset);
        if (target == document.localFixups.end()) {
            return false;
        }
        const std::size_t dataOffset = document.data.offset + target->second;
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T) ||
            dataOffset > document.bytes.size() ||
            count * sizeof(T) > document.bytes.size() - dataOffset) {
            return false;
        }
        values.resize(count);
        std::memcpy(values.data(), document.bytes.data() + dataOffset, count * sizeof(T));
        return true;
    }

    template <class T>
    void WriteRuntimeArray(
        std::array<std::byte, 0xB0>& object,
        std::size_t offset,
        std::vector<T>& values)
    {
        T* data = values.empty() ? nullptr : values.data();
        const std::int32_t size = static_cast<std::int32_t>(values.size());
        const std::uint32_t capacity = DontDeallocateFlag | static_cast<std::uint32_t>(size);
        WriteValue(object, offset, data);
        WriteValue(object, offset + 8, size);
        WriteValue(object, offset + 12, capacity);
    }
}

bool GameAnimation::Load(const char* animationPath, const char* skeletonPath, bool loop)
{
    if (!animationPath || !animationPath[0] || !skeletonPath || !skeletonPath[0]) {
        return false;
    }

    std::vector<std::uint8_t> animationBytes;
    std::vector<std::uint8_t> skeletonBytes;
    if (!ReadGameResource(animationPath, animationBytes)) {
        logger::error("Could not read game animation resource {}", animationPath);
        return false;
    }
    if (!ReadGameResource(skeletonPath, skeletonBytes)) {
        logger::error("Could not read game skeleton resource {}", skeletonPath);
        return false;
    }

    HkxDocument animationDocument{animationBytes};
    HkxDocument skeletonDocument{skeletonBytes};
    if (!ParseDocument(animationBytes, animationDocument) ||
        !ParseDocument(skeletonBytes, skeletonDocument)) {
        logger::error("Could not parse Skyrim HKX resources {} and {}", animationPath, skeletonPath);
        return false;
    }

    std::uint32_t animationObject = 0;
    std::uint32_t skeletonObject = 0;
    if (!FindObject(animationDocument, "hkaSplineCompressedAnimation", animationObject) ||
        !FindObject(skeletonDocument, "hkaSkeleton", skeletonObject)) {
        logger::error("HKX resources do not contain a spline animation and skeleton");
        return false;
    }

    const std::size_t animationBase = animationDocument.data.offset + animationObject;
    std::uint32_t animationType = 0;
    floatTrackCount = 0;
    std::uint32_t numFrames = 0;
    std::uint32_t numBlocks = 0;
    std::uint32_t maxFramesPerBlock = 0;
    std::uint32_t maskAndQuantizationSize = 0;
    float blockDuration = 0.0f;
    float blockInverseDuration = 0.0f;
    float frameDuration = 0.0f;
    std::int32_t endian = 0;
    if (!ReadValue(animationBytes, animationBase + 0x10, animationType) ||
        !ReadValue(animationBytes, animationBase + 0x14, duration) ||
        !ReadValue(animationBytes, animationBase + 0x18, transformTrackCount) ||
        !ReadValue(animationBytes, animationBase + 0x1C, floatTrackCount) ||
        !ReadValue(animationBytes, animationBase + 0x38, numFrames) ||
        !ReadValue(animationBytes, animationBase + 0x3C, numBlocks) ||
        !ReadValue(animationBytes, animationBase + 0x40, maxFramesPerBlock) ||
        !ReadValue(animationBytes, animationBase + 0x44, maskAndQuantizationSize) ||
        !ReadValue(animationBytes, animationBase + 0x48, blockDuration) ||
        !ReadValue(animationBytes, animationBase + 0x4C, blockInverseDuration) ||
        !ReadValue(animationBytes, animationBase + 0x50, frameDuration) ||
        !ReadValue(animationBytes, animationBase + 0xA8, endian) ||
        duration <= 0.0f || transformTrackCount == 0 ||
        !ReadArray(animationDocument, animationObject, 0x58, blockOffsets) ||
        !ReadArray(animationDocument, animationObject, 0x68, floatBlockOffsets) ||
        !ReadArray(animationDocument, animationObject, 0x78, transformOffsets) ||
        !ReadArray(animationDocument, animationObject, 0x88, floatOffsets) ||
        !ReadArray(animationDocument, animationObject, 0x98, compressedData)) {
        logger::error("Animation HKX data is incomplete: {}", animationPath);
        return false;
    }

    trackToBoneIndices.clear();
    std::uint32_t bindingObject = 0;
    if (FindObject(animationDocument, "hkaAnimationBinding", bindingObject)) {
        ReadArray(animationDocument, bindingObject, 0x20, trackToBoneIndices);
    }

    const std::uint32_t boneArrayOffset = skeletonObject + 0x28;
    const std::uint32_t poseArrayOffset = skeletonObject + 0x38;
    if (!ReadArray(skeletonDocument, skeletonObject, 0x18, parentIndices)) {
        logger::error("Skeleton HKX has no parent index array: {}", skeletonPath);
        return false;
    }

    std::uint32_t boneCount = 0;
    std::uint32_t poseCount = 0;
    if (!ReadValue(
            skeletonBytes,
            skeletonDocument.data.offset + boneArrayOffset + skeletonDocument.pointerSize,
            boneCount) ||
        !ReadValue(
            skeletonBytes,
            skeletonDocument.data.offset + poseArrayOffset + skeletonDocument.pointerSize,
            poseCount) ||
        boneCount == 0 || boneCount != parentIndices.size() || poseCount != boneCount) {
        logger::error("Skeleton HKX bone arrays do not match: {}", skeletonPath);
        return false;
    }

    const auto boneData = skeletonDocument.localFixups.find(boneArrayOffset);
    const auto poseData = skeletonDocument.localFixups.find(poseArrayOffset);
    if (boneData == skeletonDocument.localFixups.end() ||
        poseData == skeletonDocument.localFixups.end()) {
        return false;
    }

    boneNames.clear();
    boneNames.reserve(boneCount);
    referencePose.resize(boneCount);
    for (std::uint32_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        std::string boneName;
        if (!ReadString(skeletonDocument, boneData->second + boneIndex * 0x10, boneName)) {
            logger::error("Skeleton HKX bone {} has no name", boneIndex);
            return false;
        }
        boneNames.push_back(std::move(boneName));

        const std::size_t poseOffset =
            skeletonDocument.data.offset + poseData->second + boneIndex * 0x30;
        MeshRenderingFrameworkAPI::BoneTransform& transform = referencePose[boneIndex];
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (!ReadValue(skeletonBytes, poseOffset + axis * 4, transform.translation[axis]) ||
                !ReadValue(skeletonBytes, poseOffset + 0x20 + axis * 4, transform.scale[axis])) {
                return false;
            }
        }
        for (std::size_t component = 0; component < 4; ++component) {
            if (!ReadValue(
                    skeletonBytes,
                    poseOffset + 0x10 + component * 4,
                    transform.rotation[component])) {
                return false;
            }
        }
    }

    boneNamePointers.clear();
    boneNamePointers.reserve(boneNames.size());
    for (const std::string& boneName : boneNames) {
        boneNamePointers.push_back(boneName.c_str());
    }

    runtimeAnimation.fill(std::byte{0});
    REL::Relocation<std::uintptr_t> animationVtable{RE::VTABLE_hkaSplineCompressedAnimation[0]};
    const std::uintptr_t vtableAddress = animationVtable.address();
    WriteValue(runtimeAnimation, 0x00, vtableAddress);
    WriteValue(runtimeAnimation, 0x10, animationType);
    WriteValue(runtimeAnimation, 0x14, duration);
    WriteValue(runtimeAnimation, 0x18, transformTrackCount);
    WriteValue(runtimeAnimation, 0x1C, floatTrackCount);
    WriteValue(runtimeAnimation, 0x38, numFrames);
    WriteValue(runtimeAnimation, 0x3C, numBlocks);
    WriteValue(runtimeAnimation, 0x40, maxFramesPerBlock);
    WriteValue(runtimeAnimation, 0x44, maskAndQuantizationSize);
    WriteValue(runtimeAnimation, 0x48, blockDuration);
    WriteValue(runtimeAnimation, 0x4C, blockInverseDuration);
    WriteValue(runtimeAnimation, 0x50, frameDuration);
    WriteRuntimeArray(runtimeAnimation, 0x58, blockOffsets);
    WriteRuntimeArray(runtimeAnimation, 0x68, floatBlockOffsets);
    WriteRuntimeArray(runtimeAnimation, 0x78, transformOffsets);
    WriteRuntimeArray(runtimeAnimation, 0x88, floatOffsets);
    WriteRuntimeArray(runtimeAnimation, 0x98, compressedData);
    WriteValue(runtimeAnimation, 0xA8, endian);

    loops = loop;
    startTime = std::chrono::steady_clock::now();
    logger::info(
        "Loaded game animation {} ({} tracks, {:.3f} seconds) with skeleton {}",
        animationPath,
        transformTrackCount,
        duration,
        skeletonPath);
    return true;
}

bool GameAnimation::Sample(std::vector<MeshRenderingFrameworkAPI::BoneTransform>& pose)
{
    if (duration <= 0.0f || transformTrackCount == 0 || referencePose.empty()) {
        return false;
    }

    const std::chrono::duration<float> elapsed = std::chrono::steady_clock::now() - startTime;
    float time = elapsed.count();
    if (loops) {
        time = std::fmod(time, duration);
    } else {
        time = std::min(time, duration);
    }

    std::vector<RE::hkQsTransform> sampledTracks(transformTrackCount);
    std::vector<float> sampledFloats(floatTrackCount);
    std::uintptr_t vtableAddress = 0;
    std::memcpy(&vtableAddress, runtimeAnimation.data(), sizeof(vtableAddress));
    const std::uintptr_t* vtable = reinterpret_cast<const std::uintptr_t*>(vtableAddress);
    if (!vtable || !vtable[3]) {
        return false;
    }
    using SampleTracksFunction = void (
        const void*,
        float,
        RE::hkQsTransform*,
        float*,
        RE::hkaChunkCache*);
    SampleTracksFunction* sampleTracks = reinterpret_cast<SampleTracksFunction*>(vtable[3]);
    sampleTracks(
        runtimeAnimation.data(),
        time,
        sampledTracks.data(),
        sampledFloats.empty() ? nullptr : sampledFloats.data(),
        nullptr);

    pose = referencePose;
    for (std::uint32_t trackIndex = 0; trackIndex < transformTrackCount; ++trackIndex) {
        const std::int32_t boneIndex = trackIndex < trackToBoneIndices.size()
            ? trackToBoneIndices[trackIndex]
            : static_cast<std::int32_t>(trackIndex);
        if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= pose.size()) {
            continue;
        }

        MeshRenderingFrameworkAPI::BoneTransform& transform = pose[boneIndex];
        alignas(16) float translation[4];
        alignas(16) float rotation[4];
        alignas(16) float scale[4];
        _mm_store_ps(translation, sampledTracks[trackIndex].translation.quad);
        _mm_store_ps(rotation, sampledTracks[trackIndex].rotation.vec.quad);
        _mm_store_ps(scale, sampledTracks[trackIndex].scale.quad);
        std::copy_n(translation, 3, transform.translation);
        std::copy_n(rotation, 4, transform.rotation);
        std::copy_n(scale, 3, transform.scale);
    }
    return true;
}

const std::vector<const char*>& GameAnimation::GetBoneNames() const
{
    return boneNamePointers;
}

const std::vector<std::int16_t>& GameAnimation::GetParentIndices() const
{
    return parentIndices;
}
