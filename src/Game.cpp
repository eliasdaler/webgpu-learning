#include "Game.h"

#include "util/GltfLoader.h"
#include "util/OSUtil.h"
#include "util/SDLWebGPU.h"
#include "util/WebGPUUtil.h"

#include <SDL.h>

#include <cstdint>
#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>

#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_wgpu.h>

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

void defaultCompilationCallback(
    WGPUCompilationInfoRequestStatus status,
    WGPUCompilationInfo const* compilationInfo,
    void* userdata)
{
    const auto* label = reinterpret_cast<const char*>(userdata);
    for (std::size_t i = 0; i < compilationInfo->messageCount; ++i) {
        const auto message = compilationInfo->messages[i];
        if (message.type == (WGPUCompilationMessageType)wgpu::CompilationMessageType::Error) {
            std::cout << "Error: ";
        }
        std::cout << "module \"" << label << "\": " << message.message << "(" << message.linePos
                  << ":" << message.linePos << ")" << std::endl;
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
    @location(0) pos: vec3f,
    @location(1) uv: vec2f,
    @location(2) normal: vec3f,
};

struct PerFrameData {
    viewProj: mat4x4f,
    cameraPos: vec4f,
};

struct DirectionalLight {
    directionAndMisc: vec4f,
    colorAndIntensity: vec4f,
};

@group(0) @binding(0) var<uniform> fd: PerFrameData;
@group(0) @binding(1) var<uniform> dirLight: DirectionalLight;

struct MeshData {
    model: mat4x4f,
};

@group(2) @binding(0) var<uniform> mesh: MeshData;

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    let worldPos = mesh.model * vec4(in.position, 1.0);
    out.position = fd.viewProj * worldPos;
    out.pos = worldPos.xyz;
    out.uv = in.uv;
    out.normal = (mesh.model * vec4(in.normal, 1.0)).xyz;

    return out;
}

@group(1) @binding(0) var texture: texture_2d<f32>;
@group(1) @binding(1) var texSampler: sampler;

fn calculateSpecularBP(NoH: f32) -> f32 {
    let shininess = 32.0 * 4.0;
    return pow(NoH, shininess);
}

fn blinnPhongBRDF(diffuse: vec3f, n: vec3f, v: vec3f, l: vec3f, h: vec3f) -> vec3f {
    let Fd = diffuse;

    // specular
    // TODO: read from spec texture / pass spec param
    let specularColor = diffuse * 0.5;
    let NoH = saturate(dot(n, h));
    let Fr = specularColor * calculateSpecularBP(NoH);

    return Fd + Fr;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let diffuse = textureSample(texture, texSampler, in.uv).rgb;

    let ambient = vec3(0.1, 0.1, 0.1);

    let lightDir = -dirLight.directionAndMisc.xyz;
    let lightColor = dirLight.colorAndIntensity.rgb;

    let viewPos = fd.cameraPos.xyz;

    let n = normalize(in.normal);
    let l = normalize(lightDir - in.pos);
    let v = normalize(viewPos - in.pos);
    let h = normalize(v + l);

    let fr = blinnPhongBRDF(diffuse, n, v, l, h);

    let NoL = saturate(dot(n, l));
    var fragColor = fr * lightColor * NoL;

    // ambient
    fragColor += diffuse * ambient;

    let color = pow(fragColor, vec3(1/2.2f));
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

    const auto instanceDesc = wgpu::InstanceDescriptor{};
    instance = wgpu::CreateInstance(&instanceDesc);
    if (!instance) {
        std::cerr << "Could not initialize WebGPU!\n";
        std::exit(1);
    }

    const auto adapterOpts = wgpu::RequestAdapterOptions{};
    adapter = util::requestAdapter(instance, &adapterOpts);

    { // report supported limits
        auto supportedLimits = wgpu::SupportedLimits{};
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

    surface = std::make_unique<wgpu::Surface>(util::CreateSurfaceForSDLWindow(instance, window));

    // better for debug
    const char* const enabledToggles[] = {"dump_shaders", "disable_symbol_renaming"};
    wgpu::DawnTogglesDescriptor deviceTogglesDesc;
    deviceTogglesDesc.enabledToggles = enabledToggles;
    deviceTogglesDesc.enabledToggleCount = 2;

    // TODO: set limits
    const auto requiredLimits = wgpu::RequiredLimits{};
    const auto deviceDesc = wgpu::DeviceDescriptor{
        .nextInChain = &deviceTogglesDesc,
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

    swapChainFormat = wgpu::TextureFormat::BGRA8Unorm;
    { // init swapchain
        const auto swapChainDesc = wgpu::SwapChainDescriptor{
            .usage = wgpu::TextureUsage::RenderAttachment,
            .format = swapChainFormat,
            .width = static_cast<std::uint32_t>(params.screenWidth),
            .height = static_cast<std::uint32_t>(params.screenHeight),
            .presentMode = wgpu::PresentMode::Fifo,
        };

        swapChain =
            std::make_unique<wgpu::SwapChain>(device.CreateSwapChain(*surface, &swapChainDesc));
    }

    { // create depth dexture
        const auto textureDesc = wgpu::TextureDescriptor{
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
        const auto textureViewDesc = wgpu::TextureViewDescriptor{
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

    const auto samplerDesc = wgpu::SamplerDescriptor{
        .addressModeU = wgpu::AddressMode::Repeat,
        .addressModeV = wgpu::AddressMode::Repeat,
        .magFilter = wgpu::FilterMode::Nearest,
        .minFilter = wgpu::FilterMode::Nearest,
    };
    nearestSampler = device.CreateSampler(&samplerDesc);

    cameraPos = glm::vec3(0.0f, 3.0f, 5.0f);
    initCamera();

    createMeshDrawingPipeline();
    initSceneData();

    createYaeModel();
    createFloorTile();

    createSpriteDrawingPipeline();
    createSprite();

    initImGui();
}

void Game::initSceneData()
{
    { // per frame data buffer
        const auto bufferDesc = wgpu::BufferDescriptor{
            .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
            .size = sizeof(PerFrameData),
        };

        frameDataBuffer = device.CreateBuffer(&bufferDesc);

        const auto ud = PerFrameData{
            .viewProj = cameraProj * cameraView,
            .cameraPos = glm::vec4(cameraPos, 1.f),
        };
        queue.WriteBuffer(frameDataBuffer, 0, &ud, sizeof(PerFrameData));
    }

    { // dir light buffer
        const auto bufferDesc = wgpu::BufferDescriptor{
            .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
            .size = sizeof(DirectionalLightData),
        };

        directionalLightBuffer = device.CreateBuffer(&bufferDesc);

        const auto lightDir = glm::normalize(glm::vec3{-0.5, -0.7, -1});
        const auto lightColor = glm::vec3{1.0, 0.75, 0.38};
        const auto lightIntensity = 1.0f;

        const auto dirLightData = DirectionalLightData{
            .directionAndMisc = {lightDir, 0.f},
            .colorAndIntensity = {lightColor, lightIntensity},
        };
        queue.WriteBuffer(directionalLightBuffer, 0, &dirLightData, sizeof(DirectionalLightData));
    }

    { // per frame data
        const std::array<wgpu::BindGroupEntry, 2> bindings{{
            {
                .binding = 0,
                .buffer = frameDataBuffer,
            },
            {
                .binding = 1,
                .buffer = directionalLightBuffer,
            },
        }};
        const auto bindGroupDesc = wgpu::BindGroupDescriptor{
            .layout = perFrameDataGroupLayout.Get(),
            .entryCount = bindings.size(),
            .entries = bindings.data(),
        };

        perFrameBindGroup = device.CreateBindGroup(&bindGroupDesc);
    }
}

void Game::createYaeModel()
{
    // load model
    auto model = util::loadModel("assets/models/yae.gltf");
    // let's assume one mesh for now
    assert(model.meshes.size() == 1);
    auto& mesh = model.meshes[0];

    yaeMesh = makeGPUMesh(mesh);

    { // mesh data buffer
        const auto bufferDesc = wgpu::BufferDescriptor{
            .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
            .size = sizeof(MeshData),
        };

        modelDataBuffer = device.CreateBuffer(&bufferDesc);

        const auto md = MeshData{
            .model = glm::mat4{1.f},
        };
        queue.WriteBuffer(modelDataBuffer, 0, &md, sizeof(MeshData));
    }

    meshMaterial = makeMaterial(mesh.diffuseTexturePath, nearestSampler);

    { // mesh bind group
        const std::array<wgpu::BindGroupEntry, 1> bindings{{
            {
                .binding = 0,
                .buffer = modelDataBuffer,
            },
        }};
        const auto bindGroupDesc = wgpu::BindGroupDescriptor{
            .layout = meshGroupLayout.Get(),
            .entryCount = bindings.size(),
            .entries = bindings.data(),
        };

        meshBindGroup = device.CreateBindGroup(&bindGroupDesc);
    }
}

void Game::createMeshDrawingPipeline()
{
    { // create shader module
        auto shaderCodeDesc = wgpu::ShaderModuleWGSLDescriptor{};
        shaderCodeDesc.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
        shaderCodeDesc.code = shaderSource;

        const auto shaderDesc = wgpu::ShaderModuleDescriptor{
            .nextInChain = reinterpret_cast<wgpu::ChainedStruct*>(&shaderCodeDesc),
            .label = "model",
        };

        meshShaderModule = device.CreateShaderModule(&shaderDesc);
        meshShaderModule.GetCompilationInfo(defaultCompilationCallback, (void*)"model");
    }

    { // per frame data layout
        const std::array<wgpu::BindGroupLayoutEntry, 2> bindGroupLayoutEntries{{
            {
                .binding = 0,
                .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
                .buffer =
                    {
                        .type = wgpu::BufferBindingType::Uniform,
                    },
            },
            {
                .binding = 1,
                .visibility = wgpu::ShaderStage::Fragment,
                .buffer =
                    {
                        .type = wgpu::BufferBindingType::Uniform,
                    },
            },
        }};

        const auto bindGroupLayoutDesc = wgpu::BindGroupLayoutDescriptor{
            .entryCount = bindGroupLayoutEntries.size(),
            .entries = bindGroupLayoutEntries.data(),
        };
        perFrameDataGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDesc);
    }

    { // material data layout
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
            .entryCount = bindGroupLayoutEntries.size(),
            .entries = bindGroupLayoutEntries.data(),
        };
        materialGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDesc);
    }

    { // mesh data layout
        const std::array<wgpu::BindGroupLayoutEntry, 1> bindGroupLayoutEntries{{
            {
                .binding = 0,
                .visibility = wgpu::ShaderStage::Vertex,
                .buffer =
                    {
                        .type = wgpu::BufferBindingType::Uniform,
                    },
            },
        }};

        const auto bindGroupLayoutDesc = wgpu::BindGroupLayoutDescriptor{
            .entryCount = bindGroupLayoutEntries.size(),
            .entries = bindGroupLayoutEntries.data(),
        };
        meshGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDesc);
    }

    {
        std::array<wgpu::BindGroupLayout, 3> groupLayouts{
            perFrameDataGroupLayout,
            materialGroupLayout,
            meshGroupLayout,
        };
        const wgpu::PipelineLayoutDescriptor layoutDesc{
            .bindGroupLayoutCount = groupLayouts.size(),
            .bindGroupLayouts = groupLayouts.data(),
        };
        wgpu::RenderPipelineDescriptor pipelineDesc{
            .layout = device.CreatePipelineLayout(&layoutDesc),
            .primitive =
                {
                    .topology = wgpu::PrimitiveTopology::TriangleList,
                    .stripIndexFormat = wgpu::IndexFormat::Undefined,
                    .frontFace = wgpu::FrontFace::CCW,
                    .cullMode = wgpu::CullMode::Back,
                },
        };

        const auto depthStencilState = wgpu::DepthStencilState{
            .format = depthTextureFormat,
            .depthWriteEnabled = true,
            .depthCompare = wgpu::CompareFunction::Less,
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

        const auto vertexBufferLayout = wgpu::VertexBufferLayout{
            .arrayStride = sizeof(Mesh::Vertex),
            .stepMode = wgpu::VertexStepMode::Vertex,
            .attributeCount = static_cast<std::size_t>(vertexAttribs.size()),
            .attributes = vertexAttribs.data(),
        };

        pipelineDesc.vertex = wgpu::VertexState{
            .module = meshShaderModule,
            .entryPoint = "vs_main",
            .bufferCount = 1,
            .buffers = &vertexBufferLayout,
        };

        // fragment
        const auto blendState = wgpu::BlendState{
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

        const auto colorTarget = wgpu::ColorTargetState{
            .format = swapChainFormat,
            .blend = &blendState,
            .writeMask = wgpu::ColorWriteMask::All,
        };

        const auto fragmentState = wgpu::FragmentState{
            .module = meshShaderModule,
            .entryPoint = "fs_main",
            .targetCount = 1,
            .targets = &colorTarget,
        };
        pipelineDesc.fragment = &fragmentState;

        meshPipeline = device.CreateRenderPipeline(&pipelineDesc);
    }
}

void Game::createSpriteDrawingPipeline()
{
    { // create sprite shader module
        auto shaderCodeDesc = wgpu::ShaderModuleWGSLDescriptor{};
        shaderCodeDesc.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
        shaderCodeDesc.code = spriteShaderSource;

        const auto shaderDesc = wgpu::ShaderModuleDescriptor{
            .nextInChain = reinterpret_cast<wgpu::ChainedStruct*>(&shaderCodeDesc),
            .label = "sprite",
        };

        spriteShaderModule = device.CreateShaderModule(&shaderDesc);
        spriteShaderModule.GetCompilationInfo(defaultCompilationCallback, (void*)"sprite");
    }

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

    const auto bindGroupLayoutDesc = wgpu::BindGroupLayoutDescriptor{
        .entryCount = bindingLayoutEntries.size(),
        .entries = bindingLayoutEntries.data(),
    };
    spriteBindGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDesc);

    {
        const auto layoutDesc = wgpu::PipelineLayoutDescriptor{
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &spriteBindGroupLayout,
        };
        auto pipelineDesc = wgpu::RenderPipelineDescriptor{
            .layout = device.CreatePipelineLayout(&layoutDesc),
            .primitive =
                {
                    .topology = wgpu::PrimitiveTopology::TriangleList,
                    .stripIndexFormat = wgpu::IndexFormat::Undefined,
                    .frontFace = wgpu::FrontFace::CCW,
                    .cullMode = wgpu::CullMode::None,
                },
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

        const auto vertexBufferLayout = wgpu::VertexBufferLayout{
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
        const auto blendState = wgpu::BlendState{
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

        spritePipeline = device.CreateRenderPipeline(&pipelineDesc);
    }
}

void Game::createSprite()
{
    spriteTexture = util::loadTexture(
        device, queue, "assets/textures/shinji.png", wgpu::TextureFormat::RGBA8UnormSrgb);

    {
        const auto textureViewDesc = wgpu::TextureViewDescriptor{
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
        const auto bindGroupDesc = wgpu::BindGroupDescriptor{
            .layout = spriteBindGroupLayout.Get(),
            .entryCount = bindings.size(),
            .entries = bindings.data(),
        };

        spriteBindGroup = device.CreateBindGroup(&bindGroupDesc);
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

Game::Material Game::makeMaterial(
    const std::filesystem::path& diffusePath,
    const wgpu::Sampler& sampler)
{
    Material material;

    material.texture =
        util::loadTexture(device, queue, diffusePath, wgpu::TextureFormat::RGBA8UnormSrgb);

    { // material data
        const auto textureViewDesc = wgpu::TextureViewDescriptor{
            .format = wgpu::TextureFormat::RGBA8UnormSrgb,
            .dimension = wgpu::TextureViewDimension::e2D,
            .baseMipLevel = 0,
            .mipLevelCount = 1,
            .baseArrayLayer = 0,
            .arrayLayerCount = 1,
            .aspect = wgpu::TextureAspect::All,
        };
        const auto textureView = material.texture.CreateView(&textureViewDesc);

        const std::array<wgpu::BindGroupEntry, 2> bindings{{
            {
                .binding = 0,
                .textureView = textureView,
            },
            {
                .binding = 1,
                .sampler = sampler,
            },
        }};
        const auto bindGroupDesc = wgpu::BindGroupDescriptor{
            .layout = materialGroupLayout.Get(),
            .entryCount = bindings.size(),
            .entries = bindings.data(),
        };

        material.bindGroup = device.CreateBindGroup(&bindGroupDesc);
    }

    return material;
}

Game::GPUMesh Game::makeGPUMesh(const Mesh& cpuMesh)
{
    GPUMesh mesh;

    { // vertex buffer
        const auto bufferDesc = wgpu::BufferDescriptor{
            .usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst,
            .size = cpuMesh.vertices.size() * sizeof(Mesh::Vertex),
        };

        mesh.vertexBuffer = device.CreateBuffer(&bufferDesc);
        queue.WriteBuffer(mesh.vertexBuffer, 0, cpuMesh.vertices.data(), bufferDesc.size);
    }

    { // index buffer
        const auto bufferDesc = wgpu::BufferDescriptor{
            .usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst,
            .size = cpuMesh.indices.size() * sizeof(std::uint16_t),
        };

        mesh.indexBuffer = device.CreateBuffer(&bufferDesc);
        queue.WriteBuffer(mesh.indexBuffer, 0, cpuMesh.indices.data(), bufferDesc.size);
    }

    return mesh;
}

void Game::createFloorTile()
{
    auto tileModel = util::loadModel("assets/models/tile.gltf");
    // let's assume one mesh for now
    assert(tileModel.meshes.size() == 1);
    auto& mesh = tileModel.meshes[0];

    tileMesh = makeGPUMesh(mesh);

    { // mesh data buffer
        const auto bufferDesc = wgpu::BufferDescriptor{
            .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
            .size = sizeof(MeshData),
        };

        tileMeshDataBuffer = device.CreateBuffer(&bufferDesc);

        const auto md = MeshData{
            .model = glm::mat4{1.f},
        };
        queue.WriteBuffer(tileMeshDataBuffer, 0, &md, sizeof(MeshData));
    }

    tileMaterial = makeMaterial("assets/textures/wood_floor_deck_diff_512px.jpg", nearestSampler);

    { // mesh bind group
        const std::array<wgpu::BindGroupEntry, 1> bindings{{
            {
                .binding = 0,
                .buffer = tileMeshDataBuffer,
            },
        }};
        const auto bindGroupDesc = wgpu::BindGroupDescriptor{
            .layout = meshGroupLayout.Get(),
            .entryCount = bindings.size(),
            .entries = bindings.data(),
        };

        tileMeshBindGroup = device.CreateBindGroup(&bindGroupDesc);
    }
}

void Game::initImGui()
{
    ImGui::CreateContext();
    ImGui_ImplSDL2_InitForOther(window);
    ImGui_ImplWGPU_Init(
        device.Get(),
        3,
        (WGPUTextureFormat)swapChainFormat,
        (WGPUTextureFormat)wgpu::TextureFormat::Undefined);
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
                    ImGui_ImplSDL2_ProcessEvent(&event);
                }
            }

            ImGui_ImplWGPU_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();

            // update
            update(dt);

            accumulator -= dt;

            ImGui::Render();
        }

        // Needed to report uncaptured errors.
        // TODO: figure out how to properly use instance.ProcessEvents()
        device.Tick();
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
    { // per frame data
        PerFrameData ud{
            .viewProj = cameraProj * cameraView,
            .cameraPos = glm::vec4(cameraPos, 1.f),
        };
        queue.WriteBuffer(frameDataBuffer, 0, &ud, sizeof(PerFrameData));
    }

    { // update mesh
        modelRotationAngle += 0.5f * dt;
        glm::mat4 meshTransform{1.f};
        meshTransform = glm::rotate(meshTransform, modelRotationAngle, glm::vec3{0.f, 1.f, 0.f});

        MeshData md{
            .model = meshTransform,
        };
        queue.WriteBuffer(modelDataBuffer, 0, &md, sizeof(MeshData));
    }

    updateDevTools(dt);
}

void Game::updateDevTools(float dt)
{
    ImGui::Begin("WebGPU Dear ImGui");
    ImGui::Text("Dear ImGui is the best");
    ImGui::Button("Yes");
    ImGui::SameLine();
    ImGui::Button("Yes");
    ImGui::End();

    // ImGui::ShowDemoWindow();
}

void Game::render()
{
    // cornflower blue <3
    static const wgpu::Color clearColor{100.f / 255.f, 149.f / 255.f, 237.f / 255.f, 255.f / 255.f};

    const auto nextFrameTexture = swapChain->GetCurrentTextureView();
    if (!nextFrameTexture) {
        std::cerr << "Cannot acquire next swap chain texture" << std::endl;
        return;
    }

    const auto commandEncoderDesc = wgpu::CommandEncoderDescriptor{};
    const auto encoder = device.CreateCommandEncoder(&commandEncoderDesc);

    { // BG
        const auto mainScreenAttachment = wgpu::RenderPassColorAttachment{
            .view = nextFrameTexture,
            .loadOp = wgpu::LoadOp::Clear,
            .storeOp = wgpu::StoreOp::Store,
            .clearValue = clearColor,
        };

        const auto renderPassDesc = wgpu::RenderPassDescriptor{
            .colorAttachmentCount = 1,
            .colorAttachments = &mainScreenAttachment,
        };

        const auto renderPass = encoder.BeginRenderPass(&renderPassDesc);
        renderPass.PushDebugGroup("Draw BG");

#if 1
        { // draw sprites
            renderPass.SetPipeline(spritePipeline);
            renderPass.SetBindGroup(0, spriteBindGroup, 0, nullptr);
            renderPass.SetVertexBuffer(0, spriteVertexBuffer, 0, wgpu::kWholeSize);
            renderPass
                .SetIndexBuffer(spriteIndexBuffer, wgpu::IndexFormat::Uint16, 0, wgpu::kWholeSize);
            renderPass.DrawIndexed(spriteIndexBuffer.GetSize() / sizeof(std::uint16_t), 1, 0, 0, 0);
        }
#endif

        renderPass.PopDebugGroup();
        renderPass.End();
    }

    { // draw meshes
        const auto mainScreenAttachment = wgpu::RenderPassColorAttachment{
            .view = nextFrameTexture,
            .loadOp = wgpu::LoadOp::Load,
            .storeOp = wgpu::StoreOp::Store,
        };

        const auto depthStencilAttachment = wgpu::RenderPassDepthStencilAttachment{
            .view = depthTextureView,
            .depthLoadOp = wgpu::LoadOp::Clear,
            .depthStoreOp = wgpu::StoreOp::Store,
            .depthClearValue = 1.f,
            .depthReadOnly = false,
            // no stencil
        };

        const auto renderPassDesc = wgpu::RenderPassDescriptor{
            .colorAttachmentCount = 1,
            .colorAttachments = &mainScreenAttachment,
            .depthStencilAttachment = &depthStencilAttachment,
        };

        const auto renderPass = encoder.BeginRenderPass(&renderPassDesc);
        renderPass.PushDebugGroup("Draw meshes");

        auto drawMesh = [](const wgpu::RenderPassEncoder& renderPass, const GPUMesh& mesh) {
            renderPass.SetVertexBuffer(0, mesh.vertexBuffer, 0, wgpu::kWholeSize);
            renderPass
                .SetIndexBuffer(mesh.indexBuffer, wgpu::IndexFormat::Uint16, 0, wgpu::kWholeSize);
            renderPass.DrawIndexed(mesh.indexBuffer.GetSize() / sizeof(std::uint16_t), 1, 0, 0, 0);
        };

        { // draw mesh

            renderPass.SetPipeline(meshPipeline);
            renderPass.SetBindGroup(0, perFrameBindGroup, 0, nullptr);

            { // yae
                renderPass.SetBindGroup(1, meshMaterial.bindGroup, 0, nullptr);
                renderPass.SetBindGroup(2, meshBindGroup, 0, nullptr);
                drawMesh(renderPass, yaeMesh);
            }

            { // floor tile
                renderPass.SetBindGroup(1, tileMaterial.bindGroup, 0, nullptr);
                renderPass.SetBindGroup(2, tileMeshBindGroup, 0, nullptr);
                drawMesh(renderPass, tileMesh);
            }
        }

        renderPass.PopDebugGroup();
        renderPass.End();
    }

    {
        const auto mainScreenAttachment = wgpu::RenderPassColorAttachment{
            .view = nextFrameTexture,
            .loadOp = wgpu::LoadOp::Load,
            .storeOp = wgpu::StoreOp::Store,
        };

        const auto renderPassDesc = wgpu::RenderPassDescriptor{
            .colorAttachmentCount = 1,
            .colorAttachments = &mainScreenAttachment,
        };

        const auto renderPass = encoder.BeginRenderPass(&renderPassDesc);
        renderPass.PushDebugGroup("Draw Dear ImGui");

        ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), renderPass.Get());

        renderPass.PopDebugGroup();
        renderPass.End();
    }

    // submit
    const auto cmdBufferDesc = wgpu::CommandBufferDescriptor{};
    const auto command = encoder.Finish(&cmdBufferDesc);
    queue.Submit(1, &command);

    // flush
    swapChain->Present();
}

void Game::quit()
{
    isRunning = false;
}

void Game::cleanup()
{
    shutdownImGui();

    swapChain.reset();
    surface.reset();

    // need to destroy the device so that the app doesn't crash on SDL_Quit
    // kinda hacky, but works
    auto d = device.MoveToCHandle();
    wgpuDeviceDestroy(d);

    SDL_Quit();
}

void Game::shutdownImGui()
{
    ImGui_ImplSDL2_Shutdown();
    ImGui_ImplWGPU_Shutdown();
    ImGui::DestroyContext();
}
