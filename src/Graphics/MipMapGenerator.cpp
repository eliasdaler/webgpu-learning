#include "MipMapGenerator.h"

#include <util/WebGPUUtil.h>

namespace
{
const char* shaderSource = R"(
struct VSOutput {
  @builtin(position) position: vec4f,
  @location(0) uv: vec2f,
};

@group(0) @binding(0) var texture: texture_2d<f32>;
@group(0) @binding(1) var texSampler: sampler;

@fragment
fn fs_main(fsInput: VSOutput) -> @location(0) vec4f {
    return textureSample(texture, texSampler, fsInput.uv);
}
)";
}

void MipMapGenerator::init(
    const wgpu::Device& device,
    const wgpu::ShaderModule& fullscreenTriangleShaderModule)
{
    this->fullscreenTriangleShaderModule = fullscreenTriangleShaderModule;

    { // create shader module
        auto shaderCodeDesc = wgpu::ShaderModuleWGSLDescriptor{};
        shaderCodeDesc.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
        shaderCodeDesc.code = shaderSource;

        const auto shaderDesc = wgpu::ShaderModuleDescriptor{
            .nextInChain = reinterpret_cast<wgpu::ChainedStruct*>(&shaderCodeDesc),
            .label = "mipmap generator",
        };

        shaderModule = device.CreateShaderModule(&shaderDesc);
        shaderModule
            .GetCompilationInfo(util::defaultShaderCompilationCallback, (void*)"mipmap generator");
    }

    {
        const auto samplerDesc = wgpu::SamplerDescriptor{
            .addressModeU = wgpu::AddressMode::Repeat,
            .addressModeV = wgpu::AddressMode::Repeat,
            .magFilter = wgpu::FilterMode::Linear,
            .minFilter = wgpu::FilterMode::Linear,
        };
        linearSampler = device.CreateSampler(&samplerDesc);
    }

    { // input layout
        const std::array<wgpu::BindGroupLayoutEntry, 2> bindGroupLayoutEntries{{
            {
                .binding = 0,
                .visibility = wgpu::ShaderStage::Fragment,
                .texture =
                    {
                        .sampleType = wgpu::TextureSampleType::Float,
                        .viewDimension = wgpu::TextureViewDimension::e2D,
                    },
            },
            {
                .binding = 1,
                .visibility = wgpu::ShaderStage::Fragment,
                .sampler =
                    {
                        .type = wgpu::SamplerBindingType::Filtering,
                    },
            },
        }};

        const auto bindGroupLayoutDesc = wgpu::BindGroupLayoutDescriptor{
            .label = "texture bind group",
            .entryCount = bindGroupLayoutEntries.size(),
            .entries = bindGroupLayoutEntries.data(),
        };
        textureGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDesc);
    }

    (void)createPipelineForFormat(device, wgpu::TextureFormat::RGBA8UnormSrgb);
}

const wgpu::RenderPipeline& MipMapGenerator::createPipelineForFormat(
    const wgpu::Device& device,
    wgpu::TextureFormat format)
{
    assert(!pipelines.contains(format) && "pipeline for this format was already created");

    std::array<wgpu::BindGroupLayout, 1> groupLayouts{
        textureGroupLayout,
    };
    const wgpu::PipelineLayoutDescriptor layoutDesc{
        .bindGroupLayoutCount = groupLayouts.size(),
        .bindGroupLayouts = groupLayouts.data(),
    };
    wgpu::RenderPipelineDescriptor pipelineDesc{
        .label = "mip map generation",
        .layout = device.CreatePipelineLayout(&layoutDesc),
        .primitive =
            {
                .topology = wgpu::PrimitiveTopology::TriangleList,
                .stripIndexFormat = wgpu::IndexFormat::Undefined,
                .frontFace = wgpu::FrontFace::CCW,
                .cullMode = wgpu::CullMode::Back,
            },
    };

    pipelineDesc.vertex = wgpu::VertexState{
        .module = fullscreenTriangleShaderModule,
        .entryPoint = "vs_main",
        .bufferCount = 0,
    };

    // fragment
    const auto blendState = wgpu::BlendState{};
    const auto colorTarget = wgpu::ColorTargetState{
        .format = format,
        .blend = &blendState,
        .writeMask = wgpu::ColorWriteMask::All,
    };

    const auto fragmentState = wgpu::FragmentState{
        .module = shaderModule,
        .entryPoint = "fs_main",
        .targetCount = 1,
        .targets = &colorTarget,
    };
    pipelineDesc.fragment = &fragmentState;

    auto [it, inserted] = pipelines.emplace(format, device.CreateRenderPipeline(&pipelineDesc));
    assert(inserted);
    return it->second;
}

const wgpu::RenderPipeline& MipMapGenerator::getOrCreatePipeline(
    const wgpu::Device& device,
    wgpu::TextureFormat format)
{
    auto it = pipelines.find(format);
    if (it != pipelines.end()) {
        return it->second;
    }
    return createPipelineForFormat(device, format);
}

void MipMapGenerator::generateMips(
    const wgpu::Device& device,
    const wgpu::Queue& queue,
    const Texture& texture)
{
    const auto commandEncoderDesc = wgpu::CommandEncoderDescriptor{};
    const auto encoder = device.CreateCommandEncoder(&commandEncoderDesc);

    assert(texture.mipLevelCount >= 1);

    const auto& pipeline = getOrCreatePipeline(device, texture.format);

    if (!texture.isCubemap) {
        for (int mipLevel = 0; mipLevel < (int)texture.mipLevelCount - 1; ++mipLevel) {
            generateMip(
                device,
                queue,
                encoder,
                pipeline,
                texture.createView(mipLevel, 1),
                texture.createView(mipLevel + 1, 1));
        }
    } else {
        for (int layer = 0; layer < 6; ++layer) {
            for (int mipLevel = 0; mipLevel < (int)texture.mipLevelCount - 1; ++mipLevel) {
                generateMip(
                    device,
                    queue,
                    encoder,
                    pipeline,
                    texture.createViewForCubeLayer(mipLevel, 1, layer),
                    texture.createViewForCubeLayer(mipLevel + 1, 1, layer));
            }
        }
    }

    const auto cmdBufferDesc = wgpu::CommandBufferDescriptor{};
    const auto command = encoder.Finish(&cmdBufferDesc);
    queue.Submit(1, &command);
}

void MipMapGenerator::generateMip(
    const wgpu::Device& device,
    const wgpu::Queue& queue,
    const wgpu::CommandEncoder& encoder,
    const wgpu::RenderPipeline pipeline,
    const wgpu::TextureView& inputView,
    const wgpu::TextureView& outputView) const
{
    const std::array<wgpu::BindGroupEntry, 2> bindings{{
        {
            .binding = 0,
            .textureView = inputView,
        },
        {
            .binding = 1,
            .sampler = linearSampler,
        },
    }};
    const auto bindGroupDesc = wgpu::BindGroupDescriptor{
        .layout = textureGroupLayout.Get(),
        .entryCount = bindings.size(),
        .entries = bindings.data(),
    };
    const auto bindGroup = device.CreateBindGroup(&bindGroupDesc);

    const auto colorAttachment = wgpu::RenderPassColorAttachment{
        .view = outputView,
        .loadOp = wgpu::LoadOp::Clear,
        .storeOp = wgpu::StoreOp::Store,
    };

    const auto renderPassDesc = wgpu::RenderPassDescriptor{
        .colorAttachmentCount = 1,
        .colorAttachments = &colorAttachment,
    };

    const auto renderPass = encoder.BeginRenderPass(&renderPassDesc);
    renderPass.PushDebugGroup("Generate mips");

    renderPass.SetPipeline(pipeline);
    renderPass.SetBindGroup(0, bindGroup);
    renderPass.Draw(3);

    renderPass.PopDebugGroup();
    renderPass.End();
}
