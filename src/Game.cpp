#include "Game.h"

#include <util/GltfLoader.h>
#include <util/OSUtil.h>
#include <util/SDLWebGPU.h>
#include <util/WebGPUUtil.h>

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
    out.normal = (vec4(in.normal, 1.0)).xyz;

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

    let ambient = vec3(0.05, 0.05, 0.05);

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

    auto supportedLimits = wgpu::SupportedLimits{};
    { // report supported limits
        adapter.GetLimits(&supportedLimits);
        std::cout << "max uniform buffer size: "
                  << supportedLimits.limits.maxUniformBufferBindingSize << std::endl;
        std::cout << "max uniform buffer binding size: "
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

    // TODO: set limits?
    // it seems like not setting anything initializes everything to the minimums
    // required by the standard, which might be good, actually.
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

    initCamera();

    createMeshDrawingPipeline();
    initSceneData();

    util::LoadContext loadContext{
        .device = device,
        .queue = queue,
        .materialLayout = materialGroupLayout,
        .defaultSampler = nearestSampler,
    };

    util::loadScene(loadContext, yaeScene, "assets/models/yae.gltf");
    createEntitiesFromScene(yaeScene);

    const glm::vec3 yaePos{1.4f, 0.f, 0.f};
    auto& yae = findEntityByName("yae_mer");
    yae.transform.position = yaePos;

    util::loadScene(loadContext, scene, "assets/levels/house/house.gltf");
    createEntitiesFromScene(scene);

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
            .viewProj = camera.getViewProj(),
            .cameraPos = glm::vec4(camera.getPosition(), 1.f),
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

void Game::createEntitiesFromScene(const Scene& scene)
{
    for (const auto& nodePtr : scene.nodes) {
        if (nodePtr) {
            createEntitiesFromNode(scene, *nodePtr);
        }
    }
}

void Game::createEntitiesFromNode(const Scene& scene, const SceneNode& node)
{
    auto& e = makeNewEntity();
    e.name = node.name;
    e.transform = node.transform;

    e.scene = &scene;
    e.meshIdx = node.meshIndex;

    const auto bufferDesc = wgpu::BufferDescriptor{
        .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
        .size = sizeof(MeshData),
    };

    // TODO: do this in every frame for dynamic entities!
    e.meshDataBuffer = device.CreateBuffer(&bufferDesc);
    const auto md = MeshData{
        .model = e.transform.asMatrix(),
    };
    queue.WriteBuffer(e.meshDataBuffer, 0, &md, sizeof(MeshData));

    { // mesh bind group
        const std::array<wgpu::BindGroupEntry, 1> bindings{{
            {
                .binding = 0,
                .buffer = e.meshDataBuffer,
            },
        }};
        const auto bindGroupDesc = wgpu::BindGroupDescriptor{
            .layout = meshGroupLayout.Get(),
            .entryCount = bindings.size(),
            .entries = bindings.data(),
        };

        e.meshBindGroup = device.CreateBindGroup(&bindGroupDesc);
    }

    for (const auto& childPtr : node.children) {
        if (childPtr) {
            createEntitiesFromNode(scene, *childPtr);
        }
    }
}

Game::Entity& Game::makeNewEntity()
{
    entities.push_back(std::make_unique<Entity>());
    auto& e = *entities.back();
    e.id = entities.size() - 1;
    return e;
}

Game::Entity& Game::findEntityByName(std::string_view name) const
{
    for (const auto& ePtr : entities) {
        if (ePtr->name == name) {
            return *ePtr;
        }
    }

    throw std::runtime_error(std::string{"failed to find entity with name "} + std::string{name});
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
    static const float zNear = 0.1f;
    static const float zFar = 1000.f;
    static const float fovX = glm::radians(60.f);
    static const float aspectRatio = (float)params.screenWidth / (float)params.screenHeight;

    camera.init(glm::radians(60.f), zNear, zFar, aspectRatio);

    const auto startPos = glm::vec3{6.64f, 3.33f, 5.28f};
    cameraController.setYawPitch(-2.5f, 0.2f);
    camera.setPosition(startPos);
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
            handleInput(dt);
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

void Game::handleInput(float dt)
{
    cameraController.handleInput(camera);
}

void Game::update(float dt)
{
    cameraController.update(camera, dt);

    { // per frame data

        PerFrameData ud{
            .viewProj = camera.getViewProj(),
            .cameraPos = glm::vec4(camera.getPosition(), 1.f),
        };
        queue.WriteBuffer(frameDataBuffer, 0, &ud, sizeof(PerFrameData));
    }

    // upload mesh data (TODO: only do it if transform changed)
    for (const auto& ePtr : entities) {
        auto& e = *ePtr;

        MeshData md{
            .model = e.transform.asMatrix(),
        };
        queue.WriteBuffer(e.meshDataBuffer, 0, &md, sizeof(MeshData));
    }

    updateDevTools(dt);
}

void Game::updateDevTools(float dt)
{
    ImGui::Begin("WebGPU Dear ImGui");
    {
        const auto cameraPos = camera.getPosition();
        ImGui::Text("Camera pos: %.2f, %.2f, %.2f", cameraPos.x, cameraPos.y, cameraPos.z);
        const auto yaw = cameraController.getYaw();
        const auto pitch = cameraController.getPitch();
        ImGui::Text("Camera rotation: (yaw) %.2f, (pitch) %.2f", yaw, pitch);
    }
    ImGui::End();

    // ImGui::ShowDemoWindow();
}

void Game::render()
{
    generateDrawList();

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

#if 0
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

        renderPass.SetPipeline(meshPipeline);
        renderPass.SetBindGroup(0, perFrameBindGroup, 0);

        // TODO: sort by material?
        for (const auto& dc : drawCommands) {
            renderPass.SetBindGroup(1, dc.material.bindGroup, 0);
            renderPass.SetBindGroup(2, dc.meshBindGroup, 0);

            renderPass.SetVertexBuffer(0, dc.mesh.vertexBuffer, 0, wgpu::kWholeSize);
            renderPass.SetIndexBuffer(
                dc.mesh.indexBuffer, wgpu::IndexFormat::Uint16, 0, wgpu::kWholeSize);
            renderPass
                .DrawIndexed(dc.mesh.indexBuffer.GetSize() / sizeof(std::uint16_t), 1, 0, 0, 0);
        }

        renderPass.PopDebugGroup();
        renderPass.End();
    }

    { // Dear ImGui
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

void Game::generateDrawList()
{
    drawCommands.clear();

    for (const auto& ePtr : entities) {
        const auto& e = *ePtr;
        assert(e.scene);
        const auto& mesh = e.scene->meshes[e.meshIdx];
        for (const auto& primitive : mesh.primitives) {
            // TODO: draw frustum culling here
            const auto& material = e.scene->materials[primitive.materialIndex];
            drawCommands.push_back(DrawCommand{
                .mesh = primitive.mesh,
                .meshBindGroup = e.meshBindGroup,
                .material = material,
            });
        }
    }
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
