#include "MipMapGenerator.h"

#include <util/WebGPUUtil.h>

namespace
{
const char* shaderSource = R"(
struct VSOutput {
  @builtin(position) position: vec4f,
  @location(0) uv: vec2f,
};

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex : u32) -> VSOutput {
  let pos = array(
    vec2f( 0.0,  0.0),  // center
    vec2f( 1.0,  0.0),  // right, center
    vec2f( 0.0,  1.0),  // center, top
    // 2st triangle
    vec2f( 0.0,  1.0),  // center, top
    vec2f( 1.0,  0.0),  // right, center
    vec2f( 1.0,  1.0),  // right, top
  );

  var vsOutput: VSOutput;
  let xy = pos[vertexIndex];
  vsOutput.position = vec4f(xy * 2.0 - 1.0, 0.0, 1.0);
  vsOutput.uv = vec2f(xy.x, 1.0 - xy.y);
  return vsOutput;
}

@group(0) @binding(0) var ourTexture: texture_2d<f32>;
@group(0) @binding(1) var ourSampler: sampler;

@fragment
fn fs_main(fsInput: VSOutput) -> @location(0) vec4f {
  return textureSample(ourTexture, ourSampler, fsInput.uv);
}
)";
}

void MipMapGenerator::init(const wgpu::Device& device)
{
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
        .module = shaderModule,
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

    for (int baseMipLevel = 0; baseMipLevel < (int)texture.mipLevelCount - 1; ++baseMipLevel) {
        const std::array<wgpu::BindGroupEntry, 2> bindings{{
            {
                .binding = 0,
                .textureView = texture.createView(baseMipLevel, 1),
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
            .view = texture.createView(baseMipLevel + 1, 1),
            .loadOp = wgpu::LoadOp::Clear,
            .storeOp = wgpu::StoreOp::Store,
        };

        const auto renderPassDesc = wgpu::RenderPassDescriptor{
            .colorAttachmentCount = 1,
            .colorAttachments = &colorAttachment,
        };

        const auto renderPass = encoder.BeginRenderPass(&renderPassDesc);
        renderPass.PushDebugGroup(
            std::string("Generate mips" + std::to_string(baseMipLevel)).c_str());

        renderPass.SetPipeline(pipeline);
        renderPass.SetBindGroup(0, bindGroup);
        renderPass.Draw(6);

        renderPass.PopDebugGroup();
        renderPass.End();
    }

    const auto cmdBufferDesc = wgpu::CommandBufferDescriptor{};
    const auto command = encoder.Finish(&cmdBufferDesc);
    queue.Submit(1, &command);
}
