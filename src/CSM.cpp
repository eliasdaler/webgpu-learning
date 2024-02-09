#include "CSM.h"

#include <Graphics/FrustumCulling.h>
#include <Graphics/ShadowMapping.h>

namespace
{
struct PerFrameData {
    glm::mat4x4 viewProj;
};

struct ShaderData {
    glm::vec4 cascadeFarPlaneZs;
    std::array<glm::mat4, CSMData::NUM_SHADOW_CASCADES> lightSpaceTMs;
};
}

void initCSMData(
    CSMData& csm,
    const wgpu::Device& device,
    const wgpu::BindGroupLayout& depthOnlyPerFrameBindGroupLayout)
{
    { // CSM data
        const auto bufferDesc = wgpu::BufferDescriptor{
            .label = "CSM data buffer",
            .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
            .size = sizeof(CSMData),
        };

        csm.dataBuffer = device.CreateBuffer(&bufferDesc);
    }

    { // create CSM depth texture
        const auto textureDesc = wgpu::TextureDescriptor{
            .label = "CSM shadow map",
            .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding,
            .dimension = wgpu::TextureDimension::e2D,
            .size =
                {
                    .width = static_cast<std::uint32_t>(csm.shadowMapTextureSize),
                    .height = static_cast<std::uint32_t>(csm.shadowMapTextureSize),
                    .depthOrArrayLayers = CSMData::NUM_SHADOW_CASCADES,
                },
            .format = csm.shadowMapFormat,
            .mipLevelCount = 1,
            .sampleCount = 1,
        };
        csm.shadowMap = device.CreateTexture(&textureDesc);
    }

    // CSM per frame data buffers
    // TODO: use dynamic UBO instead?
    for (std::size_t i = 0; i < CSMData::NUM_SHADOW_CASCADES; ++i) {
        const auto bufferDesc = wgpu::BufferDescriptor{
            .label = "per frame data buffer",
            .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
            .size = sizeof(PerFrameData),
        };

        csm.perFrameDataBuffers[i] = device.CreateBuffer(&bufferDesc);

        const std::array<wgpu::BindGroupEntry, 1> bindings{{
            {
                .binding = 0,
                .buffer = csm.perFrameDataBuffers[i],
            },
        }};
        const auto bindGroupDesc = wgpu::BindGroupDescriptor{
            .layout = depthOnlyPerFrameBindGroupLayout.Get(),
            .entryCount = bindings.size(),
            .entries = bindings.data(),
        };

        csm.bindGroups[i] = device.CreateBindGroup(&bindGroupDesc);
    }
}

void updateCSMFrustums(
    CSMData& csm,
    const wgpu::Queue& queue,
    const Camera& camera,
    const glm::vec3 sunLightDir)
{
    // create subfustrum by copying everything about the main camera,
    // but changing zFar
    Camera subFrustumCamera;
    subFrustumCamera.setPosition(camera.getPosition());
    subFrustumCamera.setHeading(camera.getHeading());

    std::array<float, CSMData::NUM_SHADOW_CASCADES> percents = {0.3f, 0.8f, 1.f};
    if (camera.getZFar() > 100.f) {
        percents = {0.01f, 0.3f, 1.f};
    }

    /* std::array<float, NUM_SHADOW_CASCADES> percents{0.05f, 0.1f, 0.3f, 0.8f};
    if (camera.getZFar() > 100.f) {
        percents = {0.01f, 0.02f, 0.03f, 0.2f};
    } */

    std::array<float, CSMData::NUM_SHADOW_CASCADES> cascadeFarPlaneZs{};
    std::array<glm::mat4, CSMData::NUM_SHADOW_CASCADES> csmLightSpaceTMs{};

    for (std::size_t i = 0; i < CSMData::NUM_SHADOW_CASCADES; ++i) {
        float zNear = i == 0 ? camera.getZNear() : camera.getZNear() * percents[i - 1];
        float zFar = camera.getZFar() * percents[i];
        cascadeFarPlaneZs[i] = zFar;

        subFrustumCamera.init(camera.getFOVX(), zNear, zFar, 1.f);

        const auto corners =
            edge::calculateFrustumCornersWorldSpace(subFrustumCamera.getViewProj());
        csm.cascadeCameras[i] = calculateCSMCamera(corners, sunLightDir, csm.shadowMapTextureSize);
        csmLightSpaceTMs[i] = csm.cascadeCameras[i].getViewProj();

        PerFrameData fd{
            .viewProj = csm.cascadeCameras[i].getViewProj(),
        };
        queue.WriteBuffer(csm.perFrameDataBuffers[i], 0, &fd, sizeof(PerFrameData));
    }

    const auto csmData = CSMData::ShaderData{
        .cascadeFarPlaneZs =
            {
                cascadeFarPlaneZs[0], cascadeFarPlaneZs[1], cascadeFarPlaneZs[2], 0.f,
                // cascadeFarPlaneZs[3],
            },
        .lightSpaceTMs = csmLightSpaceTMs,
    };
    queue.WriteBuffer(csm.dataBuffer, 0, &csmData, sizeof(CSMData::ShaderData));
}
