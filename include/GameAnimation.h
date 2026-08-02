#pragma once

#include "MeshRenderingFrameworkAPI.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class GameAnimation
{
public:
    bool Load(const char* animationPath, const char* skeletonPath, bool loop);
    bool Sample(std::vector<MeshRenderingFrameworkAPI::BoneTransform>& pose);

    const std::vector<const char*>& GetBoneNames() const;
    const std::vector<std::int16_t>& GetParentIndices() const;

private:
    alignas(16) std::array<std::byte, 0xB0> runtimeAnimation{};
    std::vector<std::uint32_t> blockOffsets;
    std::vector<std::uint32_t> floatBlockOffsets;
    std::vector<std::uint32_t> transformOffsets;
    std::vector<std::uint32_t> floatOffsets;
    std::vector<std::uint8_t> compressedData;
    std::vector<std::string> boneNames;
    std::vector<const char*> boneNamePointers;
    std::vector<std::int16_t> parentIndices;
    std::vector<MeshRenderingFrameworkAPI::BoneTransform> referencePose;
    std::vector<std::int16_t> trackToBoneIndices;
    std::chrono::steady_clock::time_point startTime{};
    float duration = 0.0f;
    std::uint32_t transformTrackCount = 0;
    std::uint32_t floatTrackCount = 0;
    bool loops = true;
};
