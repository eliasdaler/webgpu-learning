#include "Game.h"

#include "util/GltfLoader.h"
#include "util/ImageLoader.h"
#include "util/OSUtil.h"
#include "util/SDLWebGPU.h"
#include "util/WebGPUUtil.h"

#include <SDL.h>

#include <cstdint>
#include <webgpu/webgpu_cpp.h>

#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>

namespace
{
void defaultValidationErrorHandler(WGPUErrorType type, char const* message, void* userdata)
{
    if (type == (WGPUErrorType)wgpu::ErrorType::NoError) {
        return;
    }

    std::cout << "Validation error: type " << type;
    if (message) {
        std::cout << " (" << message << ")";
    }
    std::cout << std::endl;
    std::exit(1);
};

// TODO: doesn't seem to be called even when shader has errors?
// (Report Dawn bug?)
void defaultCompilationCallback(
    WGPUCompilationInfoRequestStatus status,
    WGPUCompilationInfo const* compilationInfo,
    void* userdata)
{
    if (status == (WGPUCompilationInfoRequestStatus)wgpu::CompilationInfoRequestStatus::Error) {
        std::cout << compilationInfo << std::endl;
        std::exit(1);
    }
}

const char* shaderSource = R"(
struct VertexInput {
    @location(0) position: vec3f,
    @location(1) uv: vec2f,
    @location(2) normal: vec3f,
    @location(3) tangent: vec4f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

struct Uniforms {
    viewProj: mat4x4f,
    model: mat4x4f,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texture: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    var position = uniforms.viewProj * uniforms.model * vec4(in.position, 1.0);

    out.position = position;
    out.uv = in.uv;

    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let textureColor = textureSample(texture, texSampler, in.uv).rgb;
    let color = pow(textureColor, vec3(1/2.2f));
    return vec4f(color, 1.0);
}
)";

const char* spriteShaderSource = R"(
struct VertexInput {
    @location(0) position: vec2f,
    @location(1) uv: vec2f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

@group(0) @binding(0) var texture: texture_2d<f32>;
@group(0) @binding(1) var texSampler: sampler;

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    out.position = vec4f(in.position, 0.0, 1.0);
    out.uv = in.uv;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // uv has origin at bottom left
    var uv_c = in.uv;
    uv_c.y = 1 - uv_c.y;

    let textureColor = textureSample(texture, texSampler, uv_c).rgb;
    let color = pow(textureColor, vec3(1/2.2f));
    return vec4f(color, 1.0);
}
)";

/* clang-format off */
std::vector<float> pointData = {
    // x,   y,   u,   v,
    -0.5, -0.5, 0.0, 0.0,
    +0.5, -0.5, 1.0, 0.0,
    +0.5, +0.5, 1.0, 1.0,
    -0.5, +0.5, 0.0, 1.0,
};
/* clang-format on */

std::vector<std::uint16_t> indexData = {
    0,
    1,
    2, // Triangle #0
    2,
    3,
    0, // Triangle #1
};

int indexCount = static_cast<int>(indexData.size());

} // end of anonymous namespace

void Game::Params::validate()
{
    assert(screenWidth > 0);
    assert(screenHeight > 0);
}

void Game::start(Params params)
{
    params.validate();
    this->params = params;

    init();
    loop();
    cleanup();
}

void Game::init()
{
    util::setCurrentDirToExeDir();

    util::initWebGPU();

    const wgpu::InstanceDescriptor instanceDesc{};
    instance = wgpu::CreateInstance(&instanceDesc);
    if (!instance) {
        std::cerr << "Could not initialize WebGPU!\n";
        std::exit(1);
    }

    const wgpu::RequestAdapterOptions adapterOpts{};
    adapter = util::requestAdapter(instance, &adapterOpts);

    { // report supported limits
        wgpu::SupportedLimits supportedLimits{};
        adapter.GetLimits(&supportedLimits);
        std::cout << "max uniform buffer size: "
                  << supportedLimits.limits.maxUniformBufferBindingSize << std::endl;
        std::cout << "max bind groups: " << supportedLimits.limits.maxBindGroups << std::endl;
    }

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        printf("SDL could not initialize! SDL Error: %s\n", SDL_GetError());
        std::exit(1);
    }

    window = SDL_CreateWindow(
        params.windowTitle.c_str(),
        // pos
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        // size
        params.screenWidth,
        params.screenHeight,
        0);

    if (!window) {
        std::cout << "Failed to create window. SDL Error: " << SDL_GetError();
        std::exit(1);
    }

    surface = std::make_unique<wgpu::Surface>(SDL_GetWGPUSurface(instance, window));

    // TODO: set limits
    const wgpu::RequiredLimits requiredLimits{};
    const wgpu::DeviceDescriptor deviceDesc{
        .label = "Device",
        .requiredLimits = &requiredLimits,
    };

    device = util::requestDevice(adapter, &deviceDesc);

    auto onDeviceError = [](WGPUErrorType type, char const* message, void* userdata) {
        std::cout << "Uncaptured device error: type " << type;
        if (message) {
            std::cout << " (" << message << ")";
        }
        std::cout << std::endl;
    };
    device.SetUncapturedErrorCallback(onDeviceError, nullptr);

    device.SetDeviceLostCallback(
        [](WGPUDeviceLostReason reason, char const* message, void* userdata) {
            // std::cout << "WGPU device lost" << std::endl;
        },
        nullptr);

    queue = device.GetQueue();
    auto onQueueWorkDone = [](WGPUQueueWorkDoneStatus status, void* /* pUserData */) {
        // std::cout << "Queued work finished with status: " << status << std::endl;
    };
    queue.OnSubmittedWorkDone(onQueueWorkDone, nullptr);

    { // init swapchain
        swapChainFormat = wgpu::TextureFormat::BGRA8Unorm;

        const wgpu::SwapChainDescriptor swapChainDesc = {
            .usage = wgpu::TextureUsage::RenderAttachment,
            .format = swapChainFormat,
            .width = static_cast<std::uint32_t>(params.screenWidth),
            .height = static_cast<std::uint32_t>(params.screenHeight),
            .presentMode = wgpu::PresentMode::Fifo,
        };

        swapChain =
            std::make_unique<wgpu::SwapChain>(device.CreateSwapChain(*surface, &swapChainDesc));
    }

    device.PushErrorScope(wgpu::ErrorFilter::Validation);

    const auto samplerDesc = wgpu::SamplerDescriptor{
        .addressModeU = wgpu::AddressMode::Repeat,
        .addressModeV = wgpu::AddressMode::Repeat,
        .magFilter = wgpu::FilterMode::Nearest,
        .minFilter = wgpu::FilterMode::Nearest,
    };
    nearestSampler = device.CreateSampler(&samplerDesc);

    initCamera();

    { // uniform buffer
        const wgpu::BufferDescriptor bufferDesc{
            .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform,
            .size = sizeof(UniformData),
        };

        uniformBuffer = device.CreateBuffer(&bufferDesc);

        const UniformData ud{
            .viewProj = cameraProj * cameraView,
            .model = glm::mat4(1.f),
        };
        queue.WriteBuffer(uniformBuffer, 0, &ud, sizeof(UniformData));
    }

    initModelStuff();
    initSpriteStuff();

    device.PopErrorScope(defaultValidationErrorHandler, nullptr);
}

void Game::initModelStuff()
{
    { // create shader module
        wgpu::ShaderModuleWGSLDescriptor shaderCodeDesc;
        shaderCodeDesc.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
        shaderCodeDesc.code = shaderSource;

        const wgpu::ShaderModuleDescriptor shaderDesc{
            .nextInChain = reinterpret_cast<wgpu::ChainedStruct*>(&shaderCodeDesc),
            .label = "model",
        };

        shaderModule = device.CreateShaderModule(&shaderDesc);
        shaderModule.GetCompilationInfo(defaultCompilationCallback, nullptr);
    }

    // load model
    model = util::loadModel("assets/models/yae.gltf");
    // let's assume one mesh for now
    assert(model.meshes.size() == 1);
    auto& mesh = model.meshes[0];
    std::cout << "num vertices:" << mesh.vertices.size() << std::endl;

    { // load diffuse texture
        const ImageData data = util::loadImage(mesh.diffuseTexturePath);
        assert(data.pixels != nullptr);
        assert(data.channels == 4);

        const wgpu::TextureDescriptor textureDesc{
            .label = mesh.diffuseTexturePath.c_str(),
            .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
            .dimension = wgpu::TextureDimension::e2D,
            .size =
                {
                    .width = static_cast<std::uint32_t>(data.width),
                    .height = static_cast<std::uint32_t>(data.height),
                    .depthOrArrayLayers = 1,
                },
            .format = wgpu::TextureFormat::RGBA8UnormSrgb,
            .mipLevelCount = 1,
            .sampleCount = 1,
        };
        texture = device.CreateTexture(&textureDesc);

        const wgpu::ImageCopyTexture destination{
            .texture = texture,
            .mipLevel = 0,
            .origin = {0, 0, 0},
        };
        const wgpu::TextureDataLayout source{
            .bytesPerRow = static_cast<std::uint32_t>(data.width * data.channels),
            .rowsPerImage = static_cast<std::uint32_t>(data.height)};

        const auto pixelsSize = data.width * data.height * data.channels;
        queue
            .WriteTexture(&destination, (void*)data.pixels, pixelsSize, &source, &textureDesc.size);
    }

    { // create depth dexture
        const wgpu::TextureDescriptor textureDesc{
            .usage = wgpu::TextureUsage::RenderAttachment,
            .dimension = wgpu::TextureDimension::e2D,
            .size =
                {
                    .width = static_cast<std::uint32_t>(params.screenWidth),
                    .height = static_cast<std::uint32_t>(params.screenHeight),
                    .depthOrArrayLayers = 1,
                },
            .format = depthTextureFormat,
            .mipLevelCount = 1,
            .sampleCount = 1,
        };
        depthTexture = device.CreateTexture(&textureDesc);
    }

    { // create depth texture view
        const wgpu::TextureViewDescriptor textureViewDesc{
            .format = depthTextureFormat,
            .dimension = wgpu::TextureViewDimension::e2D,
            .baseMipLevel = 0,
            .mipLevelCount = 1,
            .baseArrayLayer = 0,
            .arrayLayerCount = 1,
            .aspect = wgpu::TextureAspect::DepthOnly,
        };
        depthTextureView = depthTexture.CreateView(&textureViewDesc);
    }

    wgpu::BindGroupLayout bindGroupLayout;
    { // create bind group
        const std::array<wgpu::BindGroupLayoutEntry, 3> bindingLayoutEntries{{
            {
                .binding = 0,
                .visibility = wgpu::ShaderStage::Vertex,
                .buffer =
                    {
                        .type = wgpu::BufferBindingType::Uniform,
                    },
            },
            {
                .binding = 1,
                .visibility = wgpu::ShaderStage::Fragment,
                .texture =
                    {
                        .sampleType = wgpu::TextureSampleType::Float,
                        .viewDimension = wgpu::TextureViewDimension::e2D,
                    },
            },
            {
                .binding = 2,
                .visibility = wgpu::ShaderStage::Fragment,
                .sampler =
                    {
                        .type = wgpu::SamplerBindingType::Filtering,
                    },
            },
        }};

        const wgpu::BindGroupLayoutDescriptor bindGroupLayoutDesc{
            .entryCount = bindingLayoutEntries.size(),
            .entries = bindingLayoutEntries.data(),
        };
        bindGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDesc);

        const wgpu::TextureViewDescriptor textureViewDesc{
            .format = wgpu::TextureFormat::RGBA8UnormSrgb,
            .dimension = wgpu::TextureViewDimension::e2D,
            .baseMipLevel = 0,
            .mipLevelCount = 1,
            .baseArrayLayer = 0,
            .arrayLayerCount = 1,
            .aspect = wgpu::TextureAspect::All,
        };
        const auto textureView = texture.CreateView(&textureViewDesc);

        const std::array<wgpu::BindGroupEntry, 3> bindings{{
            {
                .binding = 0,
                .buffer = uniformBuffer,
            },
            {
                .binding = 1,
                .textureView = textureView,
            },
            {
                .binding = 2,
                .sampler = nearestSampler,
            },
        }};
        const wgpu::BindGroupDescriptor bindGroupDesc{
            .layout = bindGroupLayout.Get(),
            .entryCount = bindings.size(),
            .entries = bindings.data(),
        };

        bindGroup = device.CreateBindGroup(&bindGroupDesc);
    }

    { // vertex buffer
        const wgpu::BufferDescriptor bufferDesc{
            .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Vertex,
            .size = mesh.vertices.size() * sizeof(Mesh::Vertex),
        };

        vertexBuffer = device.CreateBuffer(&bufferDesc);

        queue.WriteBuffer(vertexBuffer, 0, mesh.vertices.data(), bufferDesc.size);
    }

    { // index buffer
        util::insertFakeTriangleIfNeeded(mesh.indices);

        const wgpu::BufferDescriptor bufferDesc{
            .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Index,
            .size = mesh.indices.size() * sizeof(std::uint16_t),
        };

        indexBuffer = device.CreateBuffer(&bufferDesc);

        queue.WriteBuffer(indexBuffer, 0, mesh.indices.data(), bufferDesc.size);
    }

    {
        wgpu::RenderPipelineDescriptor pipelineDesc{};

        pipelineDesc.primitive = wgpu::PrimitiveState{
            .topology = wgpu::PrimitiveTopology::TriangleList,
            .stripIndexFormat = wgpu::IndexFormat::Undefined,
            .frontFace = wgpu::FrontFace::CCW,
            .cullMode = wgpu::CullMode::Back,
        };

        const auto depthStencilState = wgpu::DepthStencilState{
            .format = depthTextureFormat,
            .depthWriteEnabled = true,
            .depthCompare = wgpu::CompareFunction::Less,
            .stencilReadMask = 0,
            .stencilWriteMask = 0,
        };

        pipelineDesc.depthStencil = &depthStencilState;

        // vertex
        const std::array<wgpu::VertexAttribute, 4> vertexAttribs{{
            {
                // position
                .format = wgpu::VertexFormat::Float32x3,
                .offset = 0,
                .shaderLocation = 0,
            },
            {
                // uv
                .format = wgpu::VertexFormat::Float32x2,
                .offset = offsetof(Mesh::Vertex, uv),
                .shaderLocation = 1,
            },
            {
                // normal
                .format = wgpu::VertexFormat::Float32x3,
                .offset = offsetof(Mesh::Vertex, normal),
                .shaderLocation = 2,
            },
            {
                // tangent
                .format = wgpu::VertexFormat::Float32x4,
                .offset = offsetof(Mesh::Vertex, tangent),
                .shaderLocation = 3,
            },
        }};

        const wgpu::VertexBufferLayout vertexBufferLayout{
            .arrayStride = sizeof(Mesh::Vertex),
            .stepMode = wgpu::VertexStepMode::Vertex,
            .attributeCount = static_cast<std::size_t>(vertexAttribs.size()),
            .attributes = vertexAttribs.data(),
        };

        pipelineDesc.vertex = wgpu::VertexState{
            .module = shaderModule,
            .entryPoint = "vs_main",
            .bufferCount = 1,
            .buffers = &vertexBufferLayout,
        };

        // fragment
        const wgpu::BlendState blendState{
            .color =
                {
                    .operation = wgpu::BlendOperation::Add,
                    .srcFactor = wgpu::BlendFactor::SrcAlpha,
                    .dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha,
                },
            .alpha = {
                .operation = wgpu::BlendOperation::Add,
                .srcFactor = wgpu::BlendFactor::Zero,
                .dstFactor = wgpu::BlendFactor::One,
            }};

        const wgpu::ColorTargetState colorTarget = {
            .format = swapChainFormat,
            .blend = &blendState,
            .writeMask = wgpu::ColorWriteMask::All,
        };

        const wgpu::FragmentState fragmentState = {
            .module = shaderModule,
            .entryPoint = "fs_main",
            .targetCount = 1,
            .targets = &colorTarget,
        };
        pipelineDesc.fragment = &fragmentState;

        pipelineDesc.multisample.count = 1;
        pipelineDesc.multisample.mask = ~0u;
        pipelineDesc.multisample.alphaToCoverageEnabled = false;

        const wgpu::PipelineLayoutDescriptor layoutDesc{
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &bindGroupLayout,
        };
        pipelineDesc.layout = device.CreatePipelineLayout(&layoutDesc);

        pipeline = device.CreateRenderPipeline(&pipelineDesc);
    }
}

void Game::initSpriteStuff()
{
    { // create sprite shader module
        wgpu::ShaderModuleWGSLDescriptor shaderCodeDesc;
        shaderCodeDesc.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
        shaderCodeDesc.code = spriteShaderSource;

        wgpu::ShaderModuleDescriptor shaderDesc{
            .nextInChain = reinterpret_cast<wgpu::ChainedStruct*>(&shaderCodeDesc),
            .label = "sprite",
        };

        spriteShaderModule = device.CreateShaderModule(&shaderDesc);
        shaderModule.GetCompilationInfo(defaultCompilationCallback, nullptr);
    }

    {
        ImageData data = util::loadImage("assets/textures/shinji.png");
        assert(data.channels == 4);

        const wgpu::TextureDescriptor textureDesc{
            .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
            .dimension = wgpu::TextureDimension::e2D,
            .size =
                {
                    .width = static_cast<std::uint32_t>(data.width),
                    .height = static_cast<std::uint32_t>(data.height),
                    .depthOrArrayLayers = 1,
                },
            .format = wgpu::TextureFormat::RGBA8UnormSrgb,
            .mipLevelCount = 1,
            .sampleCount = 1,
        };
        spriteTexture = device.CreateTexture(&textureDesc);

        const wgpu::ImageCopyTexture destination{
            .texture = spriteTexture,
            .mipLevel = 0,
            .origin = {0, 0, 0},
        };
        const wgpu::TextureDataLayout source{
            .bytesPerRow = static_cast<std::uint32_t>(data.width * data.channels),
            .rowsPerImage = static_cast<std::uint32_t>(data.height)};

        const auto pixelsSize = data.width * data.height * data.channels;
        queue
            .WriteTexture(&destination, (void*)data.pixels, pixelsSize, &source, &textureDesc.size);
    }

    wgpu::BindGroupLayout bindGroupLayout;
    {
        const std::array<wgpu::BindGroupLayoutEntry, 2> bindingLayoutEntries{{
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

        const wgpu::BindGroupLayoutDescriptor bindGroupLayoutDesc{
            .entryCount = bindingLayoutEntries.size(),
            .entries = bindingLayoutEntries.data(),
        };
        bindGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDesc);

        const wgpu::TextureViewDescriptor textureViewDesc{
            .format = wgpu::TextureFormat::RGBA8UnormSrgb,
            .dimension = wgpu::TextureViewDimension::e2D,
            .baseMipLevel = 0,
            .mipLevelCount = 1,
            .baseArrayLayer = 0,
            .arrayLayerCount = 1,
            .aspect = wgpu::TextureAspect::All,
        };
        const auto textureView = spriteTexture.CreateView(&textureViewDesc);

        const std::array<wgpu::BindGroupEntry, 2> bindings{
            {{
                 .binding = 0,
                 .textureView = textureView,
             },
             {
                 .binding = 1,
                 .sampler = nearestSampler,
             }}};
        const wgpu::BindGroupDescriptor bindGroupDesc{
            .layout = bindGroupLayout.Get(),
            .entryCount = bindings.size(),
            .entries = bindings.data(),
        };

        spriteBindGroup = device.CreateBindGroup(&bindGroupDesc);
    }

    {
        wgpu::RenderPipelineDescriptor pipelineDesc{};

        pipelineDesc.primitive = wgpu::PrimitiveState{
            .topology = wgpu::PrimitiveTopology::TriangleList,
            .stripIndexFormat = wgpu::IndexFormat::Undefined,
            .frontFace = wgpu::FrontFace::CCW,
            .cullMode = wgpu::CullMode::None,
        };

        // vertex
        const std::array<wgpu::VertexAttribute, 2> vertexAttribs{{
            {
                // position
                .format = wgpu::VertexFormat::Float32x2,
                .offset = 0,
                .shaderLocation = 0,
            },
            {
                // uv
                .format = wgpu::VertexFormat::Float32x2,
                .offset = 2 * sizeof(float),
                .shaderLocation = 1,
            },
        }};

        const wgpu::VertexBufferLayout vertexBufferLayout{
            .arrayStride = 4 * sizeof(float),
            .stepMode = wgpu::VertexStepMode::Vertex,
            .attributeCount = static_cast<std::size_t>(vertexAttribs.size()),
            .attributes = vertexAttribs.data(),
        };

        pipelineDesc.vertex = wgpu::VertexState{
            .module = spriteShaderModule,
            .entryPoint = "vs_main",
            .bufferCount = 1,
            .buffers = &vertexBufferLayout,
        };

        // fragment
        const wgpu::BlendState blendState{
            .color =
                {
                    .operation = wgpu::BlendOperation::Add,
                    .srcFactor = wgpu::BlendFactor::SrcAlpha,
                    .dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha,
                },
            .alpha = {
                .operation = wgpu::BlendOperation::Add,
                .srcFactor = wgpu::BlendFactor::Zero,
                .dstFactor = wgpu::BlendFactor::One,
            }};

        const wgpu::ColorTargetState colorTarget = {
            .format = swapChainFormat,
            .blend = &blendState,
            .writeMask = wgpu::ColorWriteMask::All,
        };

        const wgpu::FragmentState fragmentState = {
            .module = spriteShaderModule,
            .entryPoint = "fs_main",
            .targetCount = 1,
            .targets = &colorTarget,
        };
        pipelineDesc.fragment = &fragmentState;

        pipelineDesc.multisample.count = 1;
        pipelineDesc.multisample.mask = ~0u;
        pipelineDesc.multisample.alphaToCoverageEnabled = false;

        wgpu::PipelineLayoutDescriptor layoutDesc{
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &bindGroupLayout,
        };
        pipelineDesc.layout = device.CreatePipelineLayout(&layoutDesc);

        spritePipeline = device.CreateRenderPipeline(&pipelineDesc);
    }

    { // vertex buffer
        const wgpu::BufferDescriptor bufferDesc{
            .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Vertex,
            .size = pointData.size() * sizeof(float),
        };

        spriteVertexBuffer = device.CreateBuffer(&bufferDesc);

        queue.WriteBuffer(spriteVertexBuffer, 0, pointData.data(), bufferDesc.size);
    }

    { // index buffer
        const wgpu::BufferDescriptor bufferDesc{
            .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Index,
            .size = indexData.size() * sizeof(std::uint16_t),
        };

        spriteIndexBuffer = device.CreateBuffer(&bufferDesc);

        queue.WriteBuffer(spriteIndexBuffer, 0, indexData.data(), bufferDesc.size);
    }
}

void Game::initCamera()
{
    cameraPos = glm::vec3(0.0f, 1.0f, 3.0f);
    glm::vec3 cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
    cameraDirection = glm::normalize(cameraPos - cameraTarget);

    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 cameraRight = glm::normalize(glm::cross(up, cameraDirection));
    glm::vec3 cameraUp = glm::cross(cameraDirection, cameraRight);

    cameraView = glm::lookAt(cameraPos, glm::vec3(0.0f, 0.0f, 0.0f), cameraUp);

    const auto fov = 45.f;
    const auto aspect = (float)params.screenWidth / (float)params.screenHeight;
    cameraProj = glm::perspective(glm::radians(fov), aspect, 0.1f, 100.0f);
}

void Game::loop()
{
    // Fix your timestep! game loop
    const float FPS = 60.f;
    const float dt = 1.f / FPS;

    uint32_t prev_time = SDL_GetTicks();
    float accumulator = dt; // so that we get at least 1 update before render

    isRunning = true;
    while (isRunning) {
        uint32_t new_time = SDL_GetTicks();
        const auto frame_time = (new_time - prev_time) / 1000.f;
        accumulator += frame_time;
        prev_time = new_time;

        if (accumulator > 10 * dt) { // game stopped for debug
            accumulator = dt;
        }

        while (accumulator >= dt) {
            { // event processing
                SDL_Event event;
                while (SDL_PollEvent(&event)) {
                    if (event.type == SDL_QUIT) {
                        quit();
                    }
                }
            }

            // update
            update(dt);

            accumulator -= dt;
        }

        render();

        // Delay to not overload the CPU
        const auto frameTime = (SDL_GetTicks() - prev_time) / 1000.f;
        if (dt > frameTime) {
            SDL_Delay(dt - frameTime);
        }
    }
}

void Game::update(float dt)
{
    meshRotationAngle += 0.5f * dt;

    glm::mat4 meshTransform{1.f};
    meshTransform = glm::rotate(meshTransform, meshRotationAngle, glm::vec3{0.f, 1.f, 0.f});

    UniformData ud{
        .viewProj = cameraProj * cameraView,
        .model = meshTransform,
    };

    queue.WriteBuffer(uniformBuffer, 0, &ud, sizeof(UniformData));
}

void Game::render()
{
    device.PushErrorScope(wgpu::ErrorFilter::Validation);

    // cornflower blue <3
    static const wgpu::Color clearColor{100.f / 255.f, 149.f / 255.f, 237.f / 255.f, 255.f / 255.f};

    const auto nextFrameTexture = swapChain->GetCurrentTextureView();
    if (!nextFrameTexture) {
        std::cerr << "Cannot acquire next swap chain texture" << std::endl;
        return;
    }

    const wgpu::CommandEncoderDescriptor commandEncoderDesc{};
    const auto encoder = device.CreateCommandEncoder(&commandEncoderDesc);

    const wgpu::RenderPassColorAttachment noDepthRenderPassColorAttachment = {
        .view = nextFrameTexture,
        .loadOp = wgpu::LoadOp::Clear,
        .storeOp = wgpu::StoreOp::Store,
        .clearValue = clearColor,
    };

    const wgpu::RenderPassDescriptor noDepthRenderPassDesc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &noDepthRenderPassColorAttachment,
    };

    const auto bgRenderPass = encoder.BeginRenderPass(&noDepthRenderPassDesc);

    bgRenderPass.PushDebugGroup("Draw BG");
    { // draw sprite
        bgRenderPass.SetPipeline(spritePipeline);
        bgRenderPass.SetBindGroup(0, spriteBindGroup, 0, nullptr);
        bgRenderPass.SetVertexBuffer(0, spriteVertexBuffer, 0, pointData.size() * sizeof(float));
        bgRenderPass.SetIndexBuffer(
            spriteIndexBuffer, wgpu::IndexFormat::Uint16, 0, indexCount * sizeof(std::uint16_t));
        bgRenderPass.DrawIndexed(indexCount, 1, 0, 0, 0);
    }
    bgRenderPass.PopDebugGroup();
    bgRenderPass.End();

    const wgpu::RenderPassColorAttachment renderPassColorAttachment = {
        .view = nextFrameTexture,
        .loadOp = wgpu::LoadOp::Load,
        .storeOp = wgpu::StoreOp::Store,
        .clearValue = clearColor,
    };

    const wgpu::RenderPassDepthStencilAttachment depthStencilAttachment = {
        .view = depthTextureView,
        .depthLoadOp = wgpu::LoadOp::Clear,
        .depthStoreOp = wgpu::StoreOp::Store,
        .depthClearValue = 1.f,
        .depthReadOnly = false,
        .stencilLoadOp = wgpu::LoadOp::Undefined,
        .stencilStoreOp = wgpu::StoreOp::Undefined,
        .stencilClearValue = 0,
        .stencilReadOnly = true,
    };

    const wgpu::RenderPassDescriptor renderPassDesc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &renderPassColorAttachment,
        .depthStencilAttachment = &depthStencilAttachment,
    };

    const auto renderPass = encoder.BeginRenderPass(&renderPassDesc);
    renderPass.PushDebugGroup("Draw meshes");
    { // draw mesh
        auto& mesh = model.meshes[0];

        renderPass.SetPipeline(pipeline);
        renderPass.SetBindGroup(0, bindGroup, 0, nullptr);
        renderPass.SetVertexBuffer(0, vertexBuffer, 0, mesh.vertices.size() * sizeof(Mesh::Vertex));
        renderPass.SetIndexBuffer(
            indexBuffer, wgpu::IndexFormat::Uint16, 0, mesh.indices.size() * sizeof(std::uint16_t));
        renderPass.DrawIndexed(mesh.indices.size(), 1, 0, 0, 0);
    }
    renderPass.PopDebugGroup();
    renderPass.End();

    // submit
    const wgpu::CommandBufferDescriptor cmdBufferDescriptor{};
    const auto command = encoder.Finish(&cmdBufferDescriptor);
    queue.Submit(1, &command);

    // flush
    swapChain->Present();

    device.PopErrorScope(defaultValidationErrorHandler, nullptr);
}

void Game::quit()
{
    isRunning = false;
}

void Game::cleanup()
{
    swapChain.reset();
    surface.reset();

    // need to destroy the device so that the app doesn't crash on SDL_Quit
    // kinda hacky, but works
    auto d = device.MoveToCHandle();
    wgpuDeviceDestroy(d);

    SDL_Quit();
}
