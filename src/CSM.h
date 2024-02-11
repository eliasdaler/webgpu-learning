#pragma once

#include <array>

#include <glm/vec4.hpp>

#include <webgpu/webgpu_cpp.h>

#include <Graphics/Camera.h>

struct CSMData {
    static const int NUM_SHADOW_CASCADES = 3;
    float shadowMapTextureSize{4096.f};

    struct ShaderData {
        glm::vec4 cascadeFarPlaneZs;
        std::array<glm::mat4, NUM_SHADOW_CASCADES> lightSpaceTMs;
    };
    wgpu::Buffer dataBuffer;

    wgpu::Texture shadowMap;
    wgpu::TextureFormat shadowMapFormat{wgpu::TextureFormat::Depth32Float};
    std::array<wgpu::BindGroup, NUM_SHADOW_CASCADES> bindGroups;
    std::array<wgpu::Buffer, NUM_SHADOW_CASCADES> perFrameDataBuffers;
    std::array<Camera, NUM_SHADOW_CASCADES> cascadeCameras;
};

void initCSMData(
    CSMData& csm,
    const wgpu::Device& device,
    const wgpu::BindGroupLayout& depthOnlyPerFrameBindGroupLayout);

void updateCSMFrustums(
    CSMData& csm,
    const wgpu::Queue& queue,
    const Camera& camera,
    const glm::vec3 sunLightDir);
