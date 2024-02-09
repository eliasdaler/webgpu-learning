#include "Game.h"

#include "Shaders.h"

#include <util/GltfLoader.h>
#include <util/ImageLoader.h>
#include <util/InputUtil.h>
#include <util/OSUtil.h>
#include <util/SDLWebGPU.h>
#include <util/WebGPUUtil.h>

#include <Graphics/FrustumCulling.h>
#include <Graphics/ShadowMapping.h>

#include <SDL.h>

#include <cstdint>
#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <numeric> // iota
#include <vector>

#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_wgpu.h>

#include <tracy/Tracy.hpp>

#ifdef TRACY_ENABLE
void* operator new(std ::size_t count)
{
    auto ptr = malloc(count);
    TracyAlloc(ptr, count);
    return ptr;
}
void operator delete(void* ptr) noexcept
{
    TracyFree(ptr);
    free(ptr);
}
void operator delete(void* ptr, std::size_t) noexcept
{
    TracyFree(ptr);
    free(ptr);
}
#endif

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

void allocatePerFrameDataBuffer(const wgpu::Device& device, wgpu::Buffer& buffer)
{
    const auto bufferDesc = wgpu::BufferDescriptor{
        .label = "per frame data buffer",
        .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
        .size = sizeof(Game::PerFrameData),
    };

    buffer = device.CreateBuffer(&bufferDesc);
}

void writePerFrameDataBuffer(
    const wgpu::Queue& queue,
    const wgpu::Buffer& buffer,
    const glm::vec2& screenSize,
    const Camera& camera)
{
    const auto viewProj = camera.getViewProj();
    Game::PerFrameData ud{
        .viewProj = viewProj,
        .invViewProj = glm::inverse(viewProj),
        .cameraPos = glm::vec4(camera.getPosition(), 1.f),
        .pixelSize = glm::vec2(1.f) / screenSize,
    };
    queue.WriteBuffer(buffer, 0, &ud, sizeof(Game::PerFrameData));
}

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
    // TODO: read from scene
    sunLightDir = {0.371477008, 0.470861048, 0.80018419};

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

    if (!adapter.HasFeature(wgpu::FeatureName::DepthClipControl)) {
        printf("Error: Adapter doesn't have depth-clip-control\n");
        std::exit(1);
    }

    auto supportedLimits = wgpu::SupportedLimits{};
    { // report supported limits
        adapter.GetLimits(&supportedLimits);
        std::cout << "maxUniformBufferBindingSize: "
                  << supportedLimits.limits.maxUniformBufferBindingSize << std::endl;
        std::cout << "minUniformBufferOffsetAlignment: "
                  << supportedLimits.limits.minUniformBufferOffsetAlignment << std::endl;
        std::cout << "minStorageBufferOffsetAlignment: "
                  << supportedLimits.limits.minStorageBufferOffsetAlignment << std::endl;
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

    // better debugging
    std::vector<const char*> enabledToggles{
        "disable_symbol_renaming",
        "use_user_defined_labels_in_backend",
        "dump_shaders",
        "enable_immediate_error_handling",
    };

    // change to false if something is broken
    // but otherwise, it improves debug performance 2x
    static const bool unsafeMode = true;
    if (unsafeMode) {
        enabledToggles.push_back("skip_validation");
    }

    wgpu::DawnTogglesDescriptor deviceTogglesDesc;
    deviceTogglesDesc.enabledToggles = enabledToggles.data();
    deviceTogglesDesc.enabledToggleCount = enabledToggles.size();

    requiredLimits = wgpu::RequiredLimits{};

    // use minimal offset alignment that device permits
    requiredLimits.limits.minStorageBufferOffsetAlignment =
        supportedLimits.limits.minStorageBufferOffsetAlignment;
    requiredLimits.limits.minUniformBufferOffsetAlignment =
        requiredLimits.limits.minUniformBufferOffsetAlignment;

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

    initSwapChain(vSync);

    { // create fullscreen triangle shader module
        auto shaderCodeDesc = wgpu::ShaderModuleWGSLDescriptor{};
        shaderCodeDesc.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
        shaderCodeDesc.code = fullscreenTriangleShaderSource;

        const auto shaderDesc = wgpu::ShaderModuleDescriptor{
            .nextInChain = reinterpret_cast<wgpu::ChainedStruct*>(&shaderCodeDesc),
            .label = "fullscreen triangle",
        };

        fullscreenTriangleShaderModule = device.CreateShaderModule(&shaderDesc);
        fullscreenTriangleShaderModule.GetCompilationInfo(
            util::defaultShaderCompilationCallback, (void*)"fullscreen triangle");
    }

    mipMapGenerator.init(device, fullscreenTriangleShaderModule);

    { // create depth dexture
        const auto textureDesc = wgpu::TextureDescriptor{
            .label = "depth texture",
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

        // create depth texture view
        const auto textureViewDesc = wgpu::TextureViewDescriptor{
            .format = depthTextureFormat,
            .dimension = wgpu::TextureViewDimension::e2D,
            .baseMipLevel = 0,
            .mipLevelCount = 1,
            .aspect = wgpu::TextureAspect::DepthOnly,
        };
        depthTextureView = depthTexture.CreateView(&textureViewDesc);
    }

    { // create 1px white texture
        const auto loadCtx = util::TextureLoadContext{
            .device = device,
            .queue = queue,
            .mipMapGenerator = mipMapGenerator,
        };
        glm::vec4 whiteColor{1.f, 1.f, 1.f, 1.f};
        whiteTexture =
            util::createPixelTexture(loadCtx, wgpu::TextureFormat::RGBA8Unorm, whiteColor, "white");
    }

    { // nearest sampler
        const auto samplerDesc = wgpu::SamplerDescriptor{
            .addressModeU = wgpu::AddressMode::Repeat,
            .addressModeV = wgpu::AddressMode::Repeat,
            .magFilter = wgpu::FilterMode::Nearest,
            .minFilter = wgpu::FilterMode::Nearest,
        };
        nearestSampler = device.CreateSampler(&samplerDesc);
    }

    { // bilinear sampler
        const auto samplerDesc = wgpu::SamplerDescriptor{
            .addressModeU = wgpu::AddressMode::Repeat,
            .addressModeV = wgpu::AddressMode::Repeat,
            .magFilter = wgpu::FilterMode::Linear,
            .minFilter = wgpu::FilterMode::Linear,
            .mipmapFilter = wgpu::MipmapFilterMode::Nearest,
        };
        bilinearSampler = device.CreateSampler(&samplerDesc);
    }

    { // anisotropic sampler
        const auto samplerDesc = wgpu::SamplerDescriptor{
            .addressModeU = wgpu::AddressMode::Repeat,
            .addressModeV = wgpu::AddressMode::Repeat,
            .magFilter = wgpu::FilterMode::Linear,
            .minFilter = wgpu::FilterMode::Linear,
            .mipmapFilter = wgpu::MipmapFilterMode::Linear,
            .maxAnisotropy = 8, // TODO: allow to set anisotropy?
        };
        anisotropicSampler = device.CreateSampler(&samplerDesc);
    }

    { // anisotropic sampler
        const auto samplerDesc = wgpu::SamplerDescriptor{
            .magFilter = wgpu::FilterMode::Linear,
            .minFilter = wgpu::FilterMode::Linear,
            .compare = wgpu::CompareFunction::Less,
        };
        depthCompareSampler = device.CreateSampler(&samplerDesc);
    }

    {
        const auto bufferDesc = wgpu::BufferDescriptor{
            .label = "empty storage buffer",
            .usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst,
            .size = 64, // D3D12 doesn't allow to create smaller buffers
        };
        emptyStorageBuffer = device.CreateBuffer(&bufferDesc);
    }

    initCamera();

    screenTextureFormat = wgpu::TextureFormat::RGBA16Float;
    { // create screen texture
        const auto textureDesc = wgpu::TextureDescriptor{
            .label = "screen",
            .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment,
            .dimension = wgpu::TextureDimension::e2D,
            .size =
                {
                    .width = static_cast<std::uint32_t>(params.screenWidth),
                    .height = static_cast<std::uint32_t>(params.screenHeight),
                    .depthOrArrayLayers = 1,
                },
            .format = screenTextureFormat,
        };

        auto screenTex = device.CreateTexture(&textureDesc);

        screenTexture = Texture{
            .texture = screenTex,
            .mipLevelCount = 1,
            .size = {params.screenWidth, params.screenHeight},
            .format = screenTextureFormat,
        };
        screenTextureView = screenTexture.createView();
    }

    createMeshDrawingPipeline();

    csmShadowMapFormat = wgpu::TextureFormat::Depth32Float;
    createMeshDepthOnlyDrawingPipeline();

    createSkyboxDrawingPipeline();
    createSpriteDrawingPipeline();
    createPostFXDrawingPipeline();

    { // create CSM depth texture
        const auto textureDesc = wgpu::TextureDescriptor{
            .label = "CSM shadow map",
            .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding,
            .dimension = wgpu::TextureDimension::e2D,
            .size =
                {
                    .width = static_cast<std::uint32_t>(csmTextureSize),
                    .height = static_cast<std::uint32_t>(csmTextureSize),
                    .depthOrArrayLayers = NUM_SHADOW_CASCADES,
                },
            .format = csmShadowMapFormat,
            .mipLevelCount = 1,
            .sampleCount = 1,
        };
        csmShadowMap = device.CreateTexture(&textureDesc);
    }

    initSceneData();

    { // create bind group for postFX
        const std::array<wgpu::BindGroupEntry, 3> bindings{{
            {
                .binding = 0,
                .buffer = frameDataBuffer,
            },
            {
                .binding = 1,
                .textureView = screenTextureView,
            },
            {
                .binding = 2,
                .sampler = nearestSampler,
            },
        }};
        const auto bindGroupDesc = wgpu::BindGroupDescriptor{
            .layout = postFXScreenTextureGroupLayout.Get(),
            .entryCount = bindings.size(),
            .entries = bindings.data(),
        };
        postFXBindGroup = device.CreateBindGroup(&bindGroupDesc);
    }

    const auto catoScene = loadScene("assets/models/cato.gltf");
    createEntitiesFromScene(catoScene);

    const auto yaeScene = loadScene("assets/models/yae.gltf");
    createEntitiesFromScene(yaeScene);

    const auto levelScene = loadScene("assets/levels/city/city.gltf");
    // const auto levelScene = loadScene("assets/levels/house/house.gltf");
    createEntitiesFromScene(levelScene);

    const glm::vec3 yaePos{1.4f, 0.f, -2.f};
    auto& yae = findEntityByName("yae_mer");
    yae.transform.position = yaePos;

    const glm::vec3 catoPos{1.4f, 0.0f, 0.f};
    auto& cato = findEntityByName("Cato");
    cato.transform.position = catoPos;

    createSprite(sprite, "assets/textures/tree.png");

    // load skybox
    {
        const auto loadCtx = util::TextureLoadContext{
            .device = device,
            .queue = queue,
            .mipMapGenerator = mipMapGenerator,
        };
        skyboxTexture =
            util::loadCubemap(loadCtx, "assets/textures/skybox/distant_sunset", true, "skybox");
        assert(skyboxTexture.isCubemap);

        // create bind group
        // NOTE: frameDataBuffer must already be created
        const std::array<wgpu::BindGroupEntry, 3> bindings{{
            {
                .binding = 0,
                .buffer = frameDataBuffer,
            },
            {
                .binding = 1,
                .textureView = skyboxTexture.createView(),
            },
            {
                .binding = 2,
                .sampler = bilinearSampler,
            },
        }};
        const auto bindGroupDesc = wgpu::BindGroupDescriptor{
            .layout = skyboxGroupLayout.Get(),
            .entryCount = bindings.size(),
            .entries = bindings.data(),
        };
        skyboxBindGroup = device.CreateBindGroup(&bindGroupDesc);
    }

    initImGui();
}

void Game::initSwapChain(bool vSync)
{
    swapChainFormat = wgpu::TextureFormat::BGRA8Unorm;
    { // init swapchain
        const auto swapChainDesc = wgpu::SwapChainDescriptor{
            .usage = wgpu::TextureUsage::RenderAttachment,
            .format = swapChainFormat,
            .width = static_cast<std::uint32_t>(params.screenWidth),
            .height = static_cast<std::uint32_t>(params.screenHeight),
            .presentMode = vSync ? wgpu::PresentMode::Fifo : wgpu::PresentMode::Immediate,
        };

        swapChain =
            std::make_unique<wgpu::SwapChain>(device.CreateSwapChain(*surface, &swapChainDesc));
    }
}

void Game::initSceneData()
{
    // per frame data buffer
    allocatePerFrameDataBuffer(device, frameDataBuffer);

    { // CSM data
        const auto bufferDesc = wgpu::BufferDescriptor{
            .label = "CSM data buffer",
            .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
            .size = sizeof(CSMData),
        };

        csmDataBuffer = device.CreateBuffer(&bufferDesc);
    }

    { // dir light buffer
        const auto bufferDesc = wgpu::BufferDescriptor{
            .label = "directional light data buffer",
            .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
            .size = sizeof(DirectionalLightData),
        };

        directionalLightBuffer = device.CreateBuffer(&bufferDesc);

        const auto lightDir = sunLightDir;
        const auto lightColor = glm::vec3{0.564248323, 0.455033153, 0.101168528};
        const auto lightIntensity = 1.0f;

        const auto dirLightData = DirectionalLightData{
            .directionAndMisc = {lightDir, 0.f},
            .colorAndIntensity = {lightColor, lightIntensity},
        };
        queue.WriteBuffer(directionalLightBuffer, 0, &dirLightData, sizeof(DirectionalLightData));
    }

    const auto textureViewDesc = wgpu::TextureViewDescriptor{
        .label = "CSM shadow map view",
        .format = csmShadowMapFormat,
        .dimension = wgpu::TextureViewDimension::e2DArray,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = NUM_SHADOW_CASCADES,
        .aspect = wgpu::TextureAspect::DepthOnly,
    };
    auto csmShadowMapView = csmShadowMap.CreateView(&textureViewDesc);

    { // per frame data
        const std::array<wgpu::BindGroupEntry, 5> bindings{{
            {
                .binding = 0,
                .buffer = frameDataBuffer,
            },
            {
                .binding = 1,
                .buffer = directionalLightBuffer,
            },
            {
                .binding = 2,
                .buffer = csmDataBuffer,
            },
            {
                .binding = 3,
                .textureView = csmShadowMapView,
            },
            {
                .binding = 4,
                .sampler = depthCompareSampler,
            },
        }};
        const auto bindGroupDesc = wgpu::BindGroupDescriptor{
            .layout = perFrameDataGroupLayout.Get(),
            .entryCount = bindings.size(),
            .entries = bindings.data(),
        };

        perFrameBindGroup = device.CreateBindGroup(&bindGroupDesc);
    }

    // CSM per frame data buffers
    for (std::size_t i = 0; i < NUM_SHADOW_CASCADES; ++i) {
        allocatePerFrameDataBuffer(device, csmPerFrameDataBuffers[i]);

        const std::array<wgpu::BindGroupEntry, 1> bindings{{
            {
                .binding = 0,
                .buffer = csmPerFrameDataBuffers[i],
            },
        }};
        const auto bindGroupDesc = wgpu::BindGroupDescriptor{
            .layout = depthOnlyPerFrameBindGroupLayout.Get(),
            .entryCount = bindings.size(),
            .entries = bindings.data(),
        };

        csmBindGroups[i] = device.CreateBindGroup(&bindGroupDesc);
    }
}

void Game::createMeshDrawingPipeline()
{
    { // create vertex shader module
        auto shaderCodeDesc = wgpu::ShaderModuleWGSLDescriptor{};
        shaderCodeDesc.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
        shaderCodeDesc.code = meshDrawVertexShaderSource.c_str();

        const auto shaderDesc = wgpu::ShaderModuleDescriptor{
            .nextInChain = reinterpret_cast<wgpu::ChainedStruct*>(&shaderCodeDesc),
            .label = "mesh vertex",
        };

        meshVertexShaderModule = device.CreateShaderModule(&shaderDesc);
        meshVertexShaderModule
            .GetCompilationInfo(util::defaultShaderCompilationCallback, (void*)"mesh vertex");
    }

    { // create fragment shader module
        auto shaderCodeDesc = wgpu::ShaderModuleWGSLDescriptor{};
        shaderCodeDesc.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
        shaderCodeDesc.code = meshDrawFragmentShaderSource.c_str();

        const auto shaderDesc = wgpu::ShaderModuleDescriptor{
            .nextInChain = reinterpret_cast<wgpu::ChainedStruct*>(&shaderCodeDesc),
            .label = "mesh fragment",
        };

        meshFragmentShaderModule = device.CreateShaderModule(&shaderDesc);
        meshFragmentShaderModule
            .GetCompilationInfo(util::defaultShaderCompilationCallback, (void*)"mesh fragment");
    }

    { // per frame data layout
        const std::array<wgpu::BindGroupLayoutEntry, 5> bindGroupLayoutEntries{{
            {
                // fd: PerFrameData
                .binding = 0,
                .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
                .buffer =
                    {
                        .type = wgpu::BufferBindingType::Uniform,
                    },
            },
            {
                // dirLight: DirectionalLight
                .binding = 1,
                .visibility = wgpu::ShaderStage::Fragment,
                .buffer =
                    {
                        .type = wgpu::BufferBindingType::Uniform,
                    },
            },
            {
                // csmData: CSMData
                .binding = 2,
                .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
                .buffer =
                    {
                        .type = wgpu::BufferBindingType::Uniform,
                    },
            },
            {
                // csmShadowMap
                .binding = 3,
                .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
                .texture =
                    {
                        .sampleType = wgpu::TextureSampleType::Depth,
                        .viewDimension = wgpu::TextureViewDimension::e2DArray,
                    },
            },
            {
                // csmShadowMapSampler
                .binding = 4,
                .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
                .sampler =
                    {
                        .type = wgpu::SamplerBindingType::Comparison,
                    },
            },
        }};

        const auto bindGroupLayoutDesc = wgpu::BindGroupLayoutDescriptor{
            .label = "frame bind group",
            .entryCount = bindGroupLayoutEntries.size(),
            .entries = bindGroupLayoutEntries.data(),
        };
        perFrameDataGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDesc);
    }

    { // material data layout
        const std::array<wgpu::BindGroupLayoutEntry, 3> bindGroupLayoutEntries{{
            {
                // md: MaterialData
                .binding = 0,
                .visibility = wgpu::ShaderStage::Fragment,
                .buffer =
                    {
                        .type = wgpu::BufferBindingType::Uniform,
                    },
            },
            {
                // texture: texture_2d<f32>
                .binding = 1,
                .visibility = wgpu::ShaderStage::Fragment,
                .texture =
                    {
                        .sampleType = wgpu::TextureSampleType::Float,
                        .viewDimension = wgpu::TextureViewDimension::e2D,
                    },
            },
            {
                // texSampler: sampler
                .binding = 2,
                .visibility = wgpu::ShaderStage::Fragment,
                .sampler =
                    {
                        .type = wgpu::SamplerBindingType::Filtering,
                    },
            },
        }};

        const auto bindGroupLayoutDesc = wgpu::BindGroupLayoutDescriptor{
            .label = "material bind group",
            .entryCount = bindGroupLayoutEntries.size(),
            .entries = bindGroupLayoutEntries.data(),
        };
        materialGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDesc);
    }

    { // mesh data layout
        std::vector<wgpu::BindGroupLayoutEntry> bindGroupLayoutEntries{{
            {
                // meshDat: MeshData
                .binding = 0,
                .visibility = wgpu::ShaderStage::Vertex,
                .buffer =
                    {
                        .type = wgpu::BufferBindingType::Uniform,
                    },
            },
            {
                // jointMatrices: array<mat4x4f>
                .binding = 1,
                .visibility = wgpu::ShaderStage::Vertex,
                .buffer =
                    {
                        .type = wgpu::BufferBindingType::ReadOnlyStorage,
                    },
            },
        }};

        // mesh attributes
        for (std::uint32_t i = 0; i < 6; ++i) {
            bindGroupLayoutEntries.push_back({
                .binding = 2 + i,
                .visibility = wgpu::ShaderStage::Vertex,
                .buffer =
                    {
                        .type = wgpu::BufferBindingType::ReadOnlyStorage,
                    },
            });
        }

        const auto bindGroupLayoutDesc = wgpu::BindGroupLayoutDescriptor{
            .label = "mesh bind group",
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
            .label = "mesh draw pipeline",
            .layout = device.CreatePipelineLayout(&layoutDesc),
            .primitive =
                {
                    .topology = wgpu::PrimitiveTopology::TriangleList,
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

        pipelineDesc.vertex = wgpu::VertexState{
            .module = meshVertexShaderModule,
            .entryPoint = "vs_main",
            .bufferCount = 0,
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
            .format = screenTextureFormat,
            .blend = &blendState,
            .writeMask = wgpu::ColorWriteMask::All,
        };

        const auto fragmentState = wgpu::FragmentState{
            .module = meshFragmentShaderModule,
            .entryPoint = "fs_main",
            .targetCount = 1,
            .targets = &colorTarget,
        };
        pipelineDesc.fragment = &fragmentState;

        meshPipeline = device.CreateRenderPipeline(&pipelineDesc);
    }
}

void Game::createMeshDepthOnlyDrawingPipeline()
{
    { // create vertex shader module
        auto shaderCodeDesc = wgpu::ShaderModuleWGSLDescriptor{};
        shaderCodeDesc.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
        shaderCodeDesc.code = meshDrawDepthOnlyVertexShaderSource.c_str();

        const auto shaderDesc = wgpu::ShaderModuleDescriptor{
            .nextInChain = reinterpret_cast<wgpu::ChainedStruct*>(&shaderCodeDesc),
            .label = "mesh vertex",
        };

        meshDepthOnlyVertexShaderModule = device.CreateShaderModule(&shaderDesc);
        meshDepthOnlyVertexShaderModule
            .GetCompilationInfo(util::defaultShaderCompilationCallback, (void*)"mesh vertex");
    }

    { // per frame data layout
        const std::array<wgpu::BindGroupLayoutEntry, 1> bindGroupLayoutEntries{{
            {
                // fd: PerFrameData
                .binding = 0,
                .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
                .buffer =
                    {
                        .type = wgpu::BufferBindingType::Uniform,
                    },
            },
        }};

        const auto bindGroupLayoutDesc = wgpu::BindGroupLayoutDescriptor{
            .label = "frame bind group (CSM)",
            .entryCount = bindGroupLayoutEntries.size(),
            .entries = bindGroupLayoutEntries.data(),
        };
        depthOnlyPerFrameBindGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDesc);
    }

    {
        // reused from normal mesh pipeline
        std::array<wgpu::BindGroupLayout, 2> groupLayouts{
            depthOnlyPerFrameBindGroupLayout,
            meshGroupLayout, // reused from normal mesh group layout
        };
        const wgpu::PipelineLayoutDescriptor layoutDesc{
            .bindGroupLayoutCount = groupLayouts.size(),
            .bindGroupLayouts = groupLayouts.data(),
        };

        // depth clamp
        wgpu::PrimitiveDepthClipControl dc;
        dc.sType = wgpu::SType::PrimitiveDepthClipControl;
        dc.unclippedDepth = true;

        wgpu::RenderPipelineDescriptor pipelineDesc{
            .label = "CSM mesh draw pipeline",
            .layout = device.CreatePipelineLayout(&layoutDesc),
            .primitive =
                {
                    .nextInChain = &dc,
                    .topology = wgpu::PrimitiveTopology::TriangleList,
                    .frontFace = wgpu::FrontFace::CCW,
                    .cullMode = wgpu::CullMode::Back,
                },
        };

        const auto depthStencilState = wgpu::DepthStencilState{
            .format = csmShadowMapFormat,
            .depthWriteEnabled = true,
            .depthCompare = wgpu::CompareFunction::Less,
        };

        pipelineDesc.depthStencil = &depthStencilState;

        pipelineDesc.vertex = wgpu::VertexState{
            .module = meshDepthOnlyVertexShaderModule,
            .entryPoint = "vs_main",
            .bufferCount = 0,
        };

        meshDepthOnlyPipeline = device.CreateRenderPipeline(&pipelineDesc);
    }
}

Scene Game::loadScene(const std::filesystem::path& path)
{
    const auto loadContext = util::LoadContext{
        .device = device,
        .queue = queue,
        .materialLayout = materialGroupLayout,
        .nearestSampler = nearestSampler,
        .linearSampler = anisotropicSampler,
        .whiteTexture = whiteTexture,
        .mipMapGenerator = mipMapGenerator,
        .materialCache = materialCache,
        .meshCache = meshCache,
        .requiredLimits = requiredLimits,
    };

    Scene scene;
    util::SceneLoader loader;
    loader.loadScene(loadContext, scene, path);
    return scene;
}

void Game::createEntitiesFromScene(const Scene& scene)
{
    for (const auto& nodePtr : scene.nodes) {
        if (nodePtr) {
            createEntitiesFromNode(scene, *nodePtr);
        }
    }
}

Game::EntityId Game::createEntitiesFromNode(
    const Scene& scene,
    const SceneNode& node,
    EntityId parentId)
{
    auto& e = makeNewEntity();
    e.tag = node.name;

    // transform
    {
        e.transform = node.transform;
        if (parentId == NULL_ENTITY_ID) {
            e.worldTransform = e.transform.asMatrix();
        } else {
            const auto& parent = entities[parentId];
            e.worldTransform = parent->worldTransform * node.transform.asMatrix();
        }
    }

    { // mesh
        e.meshes = scene.meshes[node.meshIndex].primitives;

        const auto bufferDesc = wgpu::BufferDescriptor{
            .label = "mesh data buffer",
            .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
            .size = sizeof(MeshData),
        };

        // TODO: do this in every frame for dynamic entities!
        e.meshDataBuffer = device.CreateBuffer(&bufferDesc);
        const auto md = MeshData{
            .model = e.worldTransform,
        };
        queue.WriteBuffer(e.meshDataBuffer, 0, &md, sizeof(MeshData));

        auto jointMatricesDataBuffer = emptyStorageBuffer;
        { // skeleton
            if (node.skinId != -1) {
                e.hasSkeleton = true;
                e.skeleton = scene.skeletons[node.skinId];

                const auto bufferDesc = wgpu::BufferDescriptor{
                    .label = "joint matrices data buffer",
                    .usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst,
                    .size = sizeof(glm::mat4) * e.skeleton.joints.size(),
                };
                e.jointMatricesDataBuffer = device.CreateBuffer(&bufferDesc);
                jointMatricesDataBuffer = e.jointMatricesDataBuffer;

                // FIXME: this is bad - we need to have some sort of cache
                // and not copy animations everywhere
                e.animations = scene.animations;

                // e.skeletonAnimator.setAnimation(e.skeleton, e.animations.at("PickUp"));
                e.skeletonAnimator.setAnimation(e.skeleton, e.animations.at("Run"));
                e.uploadJointMatricesToGPU(queue, e.skeletonAnimator.getJointMatrices());
            }
        }

        { // mesh bind group
            for (std::size_t i = 0; i < e.meshes.size(); ++i) {
                auto& mesh = meshCache.getMesh(e.meshes[i]);

                std::vector<wgpu::BindGroupEntry> bindings{{
                    {
                        .binding = 0,
                        .buffer = e.meshDataBuffer,
                    },
                    {
                        .binding = 1,
                        .buffer = jointMatricesDataBuffer,
                    },
                }};

                for (std::size_t i = 0; i < mesh.attribs.size(); ++i) {
                    const auto& attrib = mesh.attribs[i];
                    bindings.push_back({
                        .binding = 2 + static_cast<std::uint32_t>(i),
                        .buffer = mesh.vertexBuffer,
                        .offset = attrib.offset,
                        .size = attrib.size,
                    });
                }

                if (!mesh.hasSkeleton) {
                    assert(mesh.attribs.size() == 4);
                    // bind empty array to jointIds and weights
                    bindings.push_back({
                        .binding = 6,
                        .buffer = emptyStorageBuffer,
                    });
                    bindings.push_back({
                        .binding = 7,
                        .buffer = emptyStorageBuffer,
                    });
                }

                const auto bindGroupDesc = wgpu::BindGroupDescriptor{
                    .label = "mesh bind group",
                    .layout = meshGroupLayout.Get(),
                    .entryCount = bindings.size(),
                    .entries = bindings.data(),
                };

                e.meshBindGroups.push_back(device.CreateBindGroup(&bindGroupDesc));
            }
        }
    }

    { // hierarchy
        e.parentId = parentId;
        for (const auto& childPtr : node.children) {
            if (childPtr) {
                const auto childId = createEntitiesFromNode(scene, *childPtr, e.id);
                e.children.push_back(childId);
            }
        }
    }

    return e.id;
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
        if (ePtr->tag == name) {
            return *ePtr;
        }
    }

    throw std::runtime_error(std::string{"failed to find entity with name "} + std::string{name});
}

void Game::createSkyboxDrawingPipeline()
{
    { // create sprite shader module
        auto shaderCodeDesc = wgpu::ShaderModuleWGSLDescriptor{};
        shaderCodeDesc.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
        shaderCodeDesc.code = skyboxShaderSource;

        const auto shaderDesc = wgpu::ShaderModuleDescriptor{
            .nextInChain = reinterpret_cast<wgpu::ChainedStruct*>(&shaderCodeDesc),
            .label = "skybox",
        };

        skyboxShaderModule = device.CreateShaderModule(&shaderDesc);
        skyboxShaderModule
            .GetCompilationInfo(util::defaultShaderCompilationCallback, (void*)"skybox");
    }

    const std::array<wgpu::BindGroupLayoutEntry, 3> bindingLayoutEntries{{
        {
            .binding = 0,
            .visibility = wgpu::ShaderStage::Fragment,
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
                    .viewDimension = wgpu::TextureViewDimension::Cube,
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

    const auto bindGroupLayoutDesc = wgpu::BindGroupLayoutDescriptor{
        .label = "skybox bind group",
        .entryCount = bindingLayoutEntries.size(),
        .entries = bindingLayoutEntries.data(),
    };
    skyboxGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDesc);

    {
        const auto layoutDesc = wgpu::PipelineLayoutDescriptor{
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &skyboxGroupLayout,
        };
        auto pipelineDesc = wgpu::RenderPipelineDescriptor{
            .label = "skybox draw pipeline",
            .layout = device.CreatePipelineLayout(&layoutDesc),
            .primitive =
                {
                    .topology = wgpu::PrimitiveTopology::TriangleList,
                    .frontFace = wgpu::FrontFace::CCW,
                    .cullMode = wgpu::CullMode::None,
                },
        };

        pipelineDesc.vertex = wgpu::VertexState{
            .module = fullscreenTriangleShaderModule,
            .entryPoint = "vs_main",
            .bufferCount = 0,
        };

        // fragment
        const auto blendState = wgpu::BlendState{};

        const wgpu::ColorTargetState colorTarget = {
            .format = screenTextureFormat,
            .blend = &blendState,
            .writeMask = wgpu::ColorWriteMask::All,
        };

        const wgpu::FragmentState fragmentState = {
            .module = skyboxShaderModule,
            .entryPoint = "fs_main",
            .targetCount = 1,
            .targets = &colorTarget,
        };
        pipelineDesc.fragment = &fragmentState;

        skyboxPipeline = device.CreateRenderPipeline(&pipelineDesc);
    }
}

void Game::createPostFXDrawingPipeline()
{
    { // create sprite shader module
        auto shaderCodeDesc = wgpu::ShaderModuleWGSLDescriptor{};
        shaderCodeDesc.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
        shaderCodeDesc.code = postFXShaderSource;

        const auto shaderDesc = wgpu::ShaderModuleDescriptor{
            .nextInChain = reinterpret_cast<wgpu::ChainedStruct*>(&shaderCodeDesc),
            .label = "post fx",
        };

        postFXShaderModule = device.CreateShaderModule(&shaderDesc);
        postFXShaderModule
            .GetCompilationInfo(util::defaultShaderCompilationCallback, (void*)"post fx");
    }

    const std::array<wgpu::BindGroupLayoutEntry, 3> bindingLayoutEntries{{
        {
            // per frame data
            .binding = 0,
            .visibility = wgpu::ShaderStage::Fragment,
            .buffer =
                {
                    .type = wgpu::BufferBindingType::Uniform,
                },
        },
        {
            // screen texture
            .binding = 1,
            .visibility = wgpu::ShaderStage::Fragment,
            .texture =
                {
                    .sampleType = wgpu::TextureSampleType::Float,
                    .viewDimension = wgpu::TextureViewDimension::e2D,
                },
        },
        {
            // screen texture sampler
            .binding = 2,
            .visibility = wgpu::ShaderStage::Fragment,
            .sampler =
                {
                    .type = wgpu::SamplerBindingType::Filtering,
                },
        },
    }};

    const auto bindGroupLayoutDesc = wgpu::BindGroupLayoutDescriptor{
        .label = "post fx screen texture bind group",
        .entryCount = bindingLayoutEntries.size(),
        .entries = bindingLayoutEntries.data(),
    };
    postFXScreenTextureGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDesc);

    {
        const auto layoutDesc = wgpu::PipelineLayoutDescriptor{
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &postFXScreenTextureGroupLayout,
        };
        auto pipelineDesc = wgpu::RenderPipelineDescriptor{
            .label = "post fx pipeline",
            .layout = device.CreatePipelineLayout(&layoutDesc),
            .primitive =
                {
                    .topology = wgpu::PrimitiveTopology::TriangleList,
                    .cullMode = wgpu::CullMode::None,
                },
        };

        pipelineDesc.vertex = wgpu::VertexState{
            .module = fullscreenTriangleShaderModule,
            .entryPoint = "vs_main",
            .bufferCount = 0,
        };

        // fragment
        const auto blendState = wgpu::BlendState{};

        const wgpu::ColorTargetState colorTarget = {
            .format = swapChainFormat,
            .blend = &blendState,
            .writeMask = wgpu::ColorWriteMask::All,
        };

        const wgpu::FragmentState fragmentState = {
            .module = postFXShaderModule,
            .entryPoint = "fs_main",
            .targetCount = 1,
            .targets = &colorTarget,
        };
        pipelineDesc.fragment = &fragmentState;

        postFXPipeline = device.CreateRenderPipeline(&pipelineDesc);
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
        spriteShaderModule
            .GetCompilationInfo(util::defaultShaderCompilationCallback, (void*)"sprite");
    }

    const std::array<wgpu::BindGroupLayoutEntry, 3> bindingLayoutEntries{{
        {
            .binding = 0,
            .visibility = wgpu::ShaderStage::Vertex,
            .buffer =
                {
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
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
            .label = "sprite draw pipeline",
            .layout = device.CreatePipelineLayout(&layoutDesc),
            .primitive =
                {
                    .topology = wgpu::PrimitiveTopology::TriangleList,
                    .frontFace = wgpu::FrontFace::CCW,
                    .cullMode = wgpu::CullMode::None,
                },
        };

        pipelineDesc.vertex = wgpu::VertexState{
            .module = spriteShaderModule,
            .entryPoint = "vs_main",
            .bufferCount = 0,
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
            .format = screenTextureFormat,
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

void Game::createSprite(Sprite& sprite, const std::filesystem::path& texturePath)
{
    struct SpriteVertex {
        glm::vec2 pos;
        glm::vec2 uv;
    };

    /* clang-format off */
    static std::array<SpriteVertex, 4> pointData{{
        { .pos = {-0.5, -0.5}, .uv = { 0.0, 1.0 } },
        { .pos = {+0.5, -0.5}, .uv = { 1.0, 1.0 } },
        { .pos = {+0.5, +0.5}, .uv = { 1.0, 0.0 } },
        { .pos = {-0.5, +0.5}, .uv = { 0.0, 0.0 } },
    }};

    static std::array<std::uint16_t, 6> indexData = {
        0, 1, 2, // Triangle #0
        2, 3, 0, // Triangle #1
    };
    /* clang-format on */

    {
        const auto loadCtx = util::TextureLoadContext{
            .device = device,
            .queue = queue,
            .mipMapGenerator = mipMapGenerator,
        };
        sprite.texture =
            util::loadTexture(loadCtx, texturePath, wgpu::TextureFormat::RGBA8UnormSrgb, false);
    }

    { // vertex buffer
        const wgpu::BufferDescriptor bufferDesc{
            .label = "sprite vertex buffer",
            .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Storage,
            .size = pointData.size() * sizeof(SpriteVertex),
        };

        sprite.vertexBuffer = device.CreateBuffer(&bufferDesc);

        queue.WriteBuffer(sprite.vertexBuffer, 0, pointData.data(), bufferDesc.size);
    }

    { // index buffer
        const wgpu::BufferDescriptor bufferDesc{
            .label = "sprite index buffer",
            .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Index,
            .size = indexData.size() * sizeof(std::uint16_t),
        };

        sprite.indexBuffer = device.CreateBuffer(&bufferDesc);

        queue.WriteBuffer(sprite.indexBuffer, 0, indexData.data(), bufferDesc.size);
    }

    { // bind group
        const auto textureView = sprite.texture.createView();

        const std::array<wgpu::BindGroupEntry, 3> bindings{{
            {
                .binding = 0,
                .buffer = sprite.vertexBuffer,
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
        const auto bindGroupDesc = wgpu::BindGroupDescriptor{
            .label = "sprite bind group",
            .layout = spriteBindGroupLayout.Get(),
            .entryCount = bindings.size(),
            .entries = bindings.data(),
        };

        sprite.bindGroup = device.CreateBindGroup(&bindGroupDesc);
    }
}

void Game::initCamera()
{
    // static const float zNear = 0.1f;
    // static const float zFar = 800.f;
    static const float zNear = 1.f;
    static const float zFar = 64.f;
    static const float fovX = glm::radians(45.f);
    static const float aspectRatio = (float)params.screenWidth / (float)params.screenHeight;

    camera.init(glm::radians(60.f), zNear, zFar, aspectRatio);

    // const auto startPos = glm::vec3{6.64f, 3.33f, 5.28f};
    // cameraController.setYawPitch(-2.5f, 0.2f);
    const auto startPos = glm::vec3{-48.8440704, 5.05302525, 5.56558323};
    cameraController.setYawPitch(3.92699075, 0.523598909);
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

    auto& io = ImGui::GetIO();
    io.ConfigWindowsMoveFromTitleBarOnly = true;
}

void Game::loop()
{
    // Fix your timestep! game loop
    const float FPS = 60.f;
    const float dt = 1.f / FPS;

    auto prevTime = std::chrono::high_resolution_clock::now();
    float accumulator = dt; // so that we get at least 1 update before render

    isRunning = true;
    while (isRunning) {
        const auto newTime = std::chrono::high_resolution_clock::now();
        frameTime = std::chrono::duration<float>(newTime - prevTime).count();

        accumulator += frameTime;
        prevTime = newTime;

        // moving average
        float newFPS = 1.f / frameTime;
        avgFPS = std::lerp(avgFPS, newFPS, 0.1f);

        if (accumulator > 10 * dt) { // game stopped for debug
            accumulator = dt;
        }

        while (accumulator >= dt) {
            ZoneScopedN("Tick");

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

        if (frameLimit) {
            // Delay to not overload the CPU
            const auto now = std::chrono::high_resolution_clock::now();
            const auto frameTime = std::chrono::duration<float>(now - prevTime).count();
            if (dt > frameTime) {
                SDL_Delay(static_cast<std::uint32_t>(dt - frameTime));
            }
        }
    }
}

void Game::handleInput(float dt)
{
    cameraController.handleInput(camera);
}

void Game::update(float dt)
{
    ZoneScopedN("Update");

    cameraController.update(camera, dt);

    // per frame data
    auto screenSize = glm::vec2{(float)params.screenWidth, (float)params.screenHeight};
    writePerFrameDataBuffer(queue, frameDataBuffer, screenSize, camera);

    { // update cato's animation
        auto& e = findEntityByName("Cato");
        {
            ZoneScopedN("Skeletal animation");
            e.skeletonAnimator.update(e.skeleton, dt);
        }
        e.uploadJointMatricesToGPU(queue, e.skeletonAnimator.getJointMatrices());
    }

    updateEntityTransforms();
    updateCSMFrustums(camera);

    updateDevTools(dt);
}

void Game::Entity::uploadJointMatricesToGPU(
    const wgpu::Queue& queue,
    const std::vector<glm::mat4>& jointMatrices) const
{
    assert(jointMatrices.size() == skeleton.joints.size());
    queue.WriteBuffer(
        jointMatricesDataBuffer, 0, jointMatrices.data(), sizeof(glm::mat4) * jointMatrices.size());
}

void Game::updateEntityTransforms()
{
    ZoneScopedN("Update entity transforms");
    const auto I = glm::mat4{1.f};
    for (auto& ePtr : entities) {
        auto& e = *ePtr;
        if (e.parentId == NULL_MESH_ID) {
            updateEntityTransforms(e, I);
        }
    }
}

void Game::updateEntityTransforms(Entity& e, const glm::mat4& parentWorldTransform)
{
    const auto prevTransform = e.worldTransform;
    e.worldTransform = parentWorldTransform * e.transform.asMatrix();
    if (e.worldTransform == prevTransform) {
        return;
    }

    MeshData md{
        .model = e.worldTransform,
    };
    queue.WriteBuffer(e.meshDataBuffer, 0, &md, sizeof(MeshData));

    for (const auto& childId : e.children) {
        auto& child = *entities[childId];
        updateEntityTransforms(child, e.worldTransform);
    }
}

void Game::updateCSMFrustums(const Camera& camera)
{
    // create subfustrum by copying everything about the main camera,
    // but changing zFar
    Camera subFrustumCamera;
    subFrustumCamera.setPosition(camera.getPosition());
    subFrustumCamera.setHeading(camera.getHeading());

    std::array<float, NUM_SHADOW_CASCADES> percents = {0.3f, 0.8f, 1.f};
    if (camera.getZFar() > 100.f) {
        percents = {0.01f, 0.3f, 1.f};
    }

    /* std::array<float, NUM_SHADOW_CASCADES> percents{0.05f, 0.1f, 0.3f, 0.8f};
    if (camera.getZFar() > 100.f) {
        percents = {0.01f, 0.02f, 0.03f, 0.2f};
    } */

    std::array<float, NUM_SHADOW_CASCADES> cascadeFarPlaneZs{};
    std::array<glm::mat4, NUM_SHADOW_CASCADES> csmLightSpaceTMs{};

    for (std::size_t i = 0; i < NUM_SHADOW_CASCADES; ++i) {
        float zNear = i == 0 ? camera.getZNear() : camera.getZNear() * percents[i - 1];
        float zFar = camera.getZFar() * percents[i];
        cascadeFarPlaneZs[i] = zFar;

        subFrustumCamera.init(camera.getFOVX(), zNear, zFar, 1.f);

        const auto corners =
            edge::calculateFrustumCornersWorldSpace(subFrustumCamera.getViewProj());
        csmCameras[i] = calculateCSMCamera(corners, sunLightDir, csmTextureSize);
        csmLightSpaceTMs[i] = csmCameras[i].getViewProj();

        writePerFrameDataBuffer(
            queue,
            csmPerFrameDataBuffers[i],
            glm::vec2{csmTextureSize, csmTextureSize},
            csmCameras[i]);
    }

    const auto csmData = CSMData{
        .cascadeFarPlaneZs =
            {
                cascadeFarPlaneZs[0],
                cascadeFarPlaneZs[1],
                cascadeFarPlaneZs[2],
                cascadeFarPlaneZs[3],
            },
        .lightSpaceTMs = csmLightSpaceTMs,
    };
    queue.WriteBuffer(csmDataBuffer, 0, &csmData, sizeof(CSMData));
}

namespace
{
void updateSkeletonDisplayUI(const Skeleton& skeleton, JointId jointId)
{
    const auto& jointName = skeleton.jointNames[jointId];

    ImGui::PushID(jointId);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_DefaultOpen;

    const auto& joint = skeleton.joints[jointId];
    const auto& children = skeleton.hierarchy[jointId].children;
    if (children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    const auto label = jointName + ", id = " + std::to_string(jointId);
    if (ImGui::TreeNodeEx(label.c_str(), flags)) {
        for (const auto& childIdx : children) {
            updateSkeletonDisplayUI(skeleton, childIdx);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void updateSkeletonDisplayUI(const Skeleton& skeleton)
{
    updateSkeletonDisplayUI(skeleton, ROOT_JOINT_ID);
}
}

void Game::updateDevTools(float dt)
{
    if (displayFPSDelay > 0.f) {
        displayFPSDelay -= dt;
    } else {
        displayFPSDelay = 1.f;
        displayedFPS = avgFPS;
    }

    ImGui::Begin("WebGPU Dear ImGui");
    {
        // ImGui::Text("Frame time: %.1f ms", frameTime * 1000.f);
        ImGui::Text("FPS: %d", (int)displayedFPS);
        if (ImGui::Checkbox("VSync", &vSync)) {
            initSwapChain(vSync);
        }
        ImGui::Checkbox("Frame limit", &frameLimit);
        ImGui::Checkbox("Shadows", &drawShadows);

        const auto cameraPos = camera.getPosition();
        ImGui::Text("Camera pos: %.2f, %.2f, %.2f", cameraPos.x, cameraPos.y, cameraPos.z);
        const auto yaw = cameraController.getYaw();
        const auto pitch = cameraController.getPitch();
        ImGui::Text("Camera rotation: (yaw) %.2f, (pitch) %.2f", yaw, pitch);
    }
    ImGui::End();

    ImGui::Begin("Animation");
    {
        auto& e = findEntityByName("Cato");
        if (ImGui::BeginCombo("Animation", e.skeletonAnimator.getCurrentAnimationName().c_str())) {
            for (const auto& [an, a] : e.animations) {
                if (ImGui::Selectable(an.c_str())) {
                    e.skeletonAnimator.setAnimation(e.skeleton, a);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Text("duration = %.2f", e.skeletonAnimator.getAnimation()->duration);

        float timeNormalized = e.skeletonAnimator.getNormalizedProgress();
        if (ImGui::SliderFloat("time", &timeNormalized, 0.f, 1.f)) {
            e.skeletonAnimator.setNormalizedProgress(timeNormalized);
        }

        if (ImGui::CollapsingHeader("Skeleton")) {
            updateSkeletonDisplayUI(e.skeleton);
        }
    }

    ImGui::End();

    ImGui::ShowDemoWindow();
}

void Game::render()
{
    generateDrawList();

    ZoneScopedN("Draw");

    // cornflower blue <3
    static const wgpu::Color clearColor{100.f / 255.f, 149.f / 255.f, 237.f / 255.f, 255.f / 255.f};

    const auto nextFrameTexture = swapChain->GetCurrentTextureView();
    if (!nextFrameTexture) {
        std::cerr << "Cannot acquire next swap chain texture" << std::endl;
        return;
    }

    const auto commandEncoderDesc = wgpu::CommandEncoderDescriptor{};
    const auto encoder = device.CreateCommandEncoder(&commandEncoderDesc);

    for (std::size_t i = 0; i < NUM_SHADOW_CASCADES; ++i) { // draw CSM
        // create view
        const auto textureViewDesc = wgpu::TextureViewDescriptor{
            .label = "CSM shadow map view",
            .format = csmShadowMapFormat,
            .dimension = wgpu::TextureViewDimension::e2D,
            .baseMipLevel = 0,
            .mipLevelCount = 1,
            .baseArrayLayer = (std::uint32_t)i,
            .arrayLayerCount = 1,
            .aspect = wgpu::TextureAspect::DepthOnly,
        };
        csmShadowMapView = csmShadowMap.CreateView(&textureViewDesc);

        const auto depthStencilAttachment = wgpu::RenderPassDepthStencilAttachment{
            .view = csmShadowMapView,
            .depthLoadOp = wgpu::LoadOp::Clear,
            .depthStoreOp = wgpu::StoreOp::Store,
            .depthClearValue = 1.f,
            .depthReadOnly = false,
            // no stencil
        };

        const auto renderPassDesc = wgpu::RenderPassDescriptor{
            .depthStencilAttachment = &depthStencilAttachment,
        };

        {
            ZoneScopedN("CSM render pass");

            const auto renderPass = encoder.BeginRenderPass(&renderPassDesc);

            if (drawShadows) {
                renderPass.PushDebugGroup("Draw meshes to CSM");

                renderPass.SetPipeline(meshDepthOnlyPipeline);
                renderPass.SetBindGroup(0, csmBindGroups[i]);

                auto prevMaterialIdx = NULL_MATERIAL_ID;
                auto prevMeshId = NULL_MESH_ID;

                const auto frustum = edge::createFrustumFromCamera(csmCameras[i]);

                for (const auto& dcIdx : sortedDrawCommands) {
                    const auto& dc = drawCommands[dcIdx];

                    // hack: don't cull big objects, because shadows from them might disappear
                    if (dc.worldBoundingSphere.radius < 2.f &&
                        !edge::isInFrustum(frustum, dc.worldBoundingSphere)) {
                        continue;
                    }

                    renderPass.SetBindGroup(1, dc.meshBindGroup);

                    if (dc.meshId != prevMeshId) {
                        prevMeshId = dc.meshId;
                        renderPass.SetIndexBuffer(
                            dc.mesh.indexBuffer, wgpu::IndexFormat::Uint16, 0, wgpu::kWholeSize);
                    }

                    renderPass.DrawIndexed(dc.mesh.indexBufferSize, 1, 0, 0, 0);
                }

                renderPass.PopDebugGroup();
            }

            renderPass.End();
        }
    }

    { // draw sky
        const auto mainScreenAttachment = wgpu::RenderPassColorAttachment{
            .view = screenTextureView,
            .loadOp = wgpu::LoadOp::Clear,
            .storeOp = wgpu::StoreOp::Store,
            .clearValue = clearColor,
        };

        const auto renderPassDesc = wgpu::RenderPassDescriptor{
            .colorAttachmentCount = 1,
            .colorAttachments = &mainScreenAttachment,
        };

        {
            ZoneScopedN("Sky pass");

            const auto renderPass = encoder.BeginRenderPass(&renderPassDesc);
            renderPass.PushDebugGroup("Sky pass");

            renderPass.SetPipeline(skyboxPipeline);
            renderPass.SetBindGroup(0, skyboxBindGroup);
            renderPass.Draw(3);

            renderPass.PopDebugGroup();
            renderPass.End();
        }
    }

    { // draw meshes
        const auto mainScreenAttachment = wgpu::RenderPassColorAttachment{
            .view = screenTextureView,
            .loadOp = wgpu::LoadOp::Load,
            .storeOp = wgpu::StoreOp::Store,
            .clearValue = clearColor,
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

        {
            ZoneScopedN("Mesh draw render pass");

            const auto renderPass = encoder.BeginRenderPass(&renderPassDesc);
            renderPass.PushDebugGroup("Draw meshes");

            renderPass.SetPipeline(meshPipeline);
            renderPass.SetBindGroup(0, perFrameBindGroup);

            auto prevMaterialIdx = NULL_MATERIAL_ID;
            auto prevMeshId = NULL_MESH_ID;

            const auto frustum = edge::createFrustumFromCamera(camera);

            for (const auto& dcIdx : sortedDrawCommands) {
                const auto& dc = drawCommands[dcIdx];
                if (!edge::isInFrustum(frustum, dc.worldBoundingSphere)) {
                    continue;
                }

                if (dc.mesh.materialId != prevMaterialIdx) {
                    prevMaterialIdx = dc.mesh.materialId;
                    const auto& material = materialCache.getMaterial(dc.mesh.materialId);
                    renderPass.SetBindGroup(1, material.bindGroup);
                }

                renderPass.SetBindGroup(2, dc.meshBindGroup);

                if (dc.meshId != prevMeshId) {
                    prevMeshId = dc.meshId;
                    renderPass.SetIndexBuffer(
                        dc.mesh.indexBuffer, wgpu::IndexFormat::Uint16, 0, wgpu::kWholeSize);
                }

                renderPass.DrawIndexed(dc.mesh.indexBufferSize, 1, 0, 0, 0);
            }

            renderPass.PopDebugGroup();
            renderPass.End();
        }
    }

#if 0
    { // sprite
        const auto mainScreenAttachment = wgpu::RenderPassColorAttachment{
            .view = screenTextureView,
            .loadOp = wgpu::LoadOp::Load,
            .storeOp = wgpu::StoreOp::Store,
        };

        const auto renderPassDesc = wgpu::RenderPassDescriptor{
            .colorAttachmentCount = 1,
            .colorAttachments = &mainScreenAttachment,
        };

        const auto renderPass = encoder.BeginRenderPass(&renderPassDesc);
        renderPass.PushDebugGroup("Draw sprites");

        renderPass.SetPipeline(spritePipeline);

        { // draw sprites

            renderPass.SetBindGroup(0, sprite.bindGroup);
            renderPass
                .SetIndexBuffer(sprite.indexBuffer, wgpu::IndexFormat::Uint16, 0, wgpu::kWholeSize);
            renderPass
                .DrawIndexed(sprite.indexBuffer.GetSize() / sizeof(std::uint16_t), 1, 0, 0, 0);
        }

        renderPass.PopDebugGroup();
        renderPass.End();
    }
#endif

    { // post fx
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

        {
            ZoneScopedN("Post FX pass");

            const auto renderPass = encoder.BeginRenderPass(&renderPassDesc);
            renderPass.PushDebugGroup("Post FX pass");

            renderPass.SetPipeline(postFXPipeline);
            renderPass.SetBindGroup(0, postFXBindGroup);
            renderPass.Draw(3);

            renderPass.PopDebugGroup();
            renderPass.End();
        }
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

    FrameMark;
}

void Game::generateDrawList()
{
    ZoneScopedN("Generate draw list");

    drawCommands.clear();

    for (const auto& ePtr : entities) {
        const auto& e = *ePtr;
        const auto transformMat = e.transform.asMatrix();

        for (std::size_t meshIdx = 0; meshIdx < e.meshes.size(); ++meshIdx) {
            const auto& mesh = meshCache.getMesh(e.meshes[meshIdx]);
            // TODO: draw frustum culling here
            const auto& material = materialCache.getMaterial(mesh.materialId);

            const auto worldBoundingSphere = edge::
                calculateBoundingSphereWorld(transformMat, mesh.boundingSphere, e.hasSkeleton);

            drawCommands.push_back(DrawCommand{
                .mesh = mesh,
                .meshBindGroup = e.meshBindGroups[meshIdx],
                .meshId = e.meshes[meshIdx],
                .worldBoundingSphere = worldBoundingSphere,
            });
        }
    }

    sortDrawList();
}

void Game::sortDrawList()
{
    sortedDrawCommands.clear();
    sortedDrawCommands.resize(drawCommands.size());
    std::iota(sortedDrawCommands.begin(), sortedDrawCommands.end(), 0);

    std::sort(
        sortedDrawCommands.begin(),
        sortedDrawCommands.end(),
        [this](const auto& i1, const auto& i2) {
            const auto& dc1 = drawCommands[i1];
            const auto& dc2 = drawCommands[i2];
            if (dc1.mesh.materialId == dc2.mesh.materialId) {
                return dc1.meshId < dc2.meshId;
            }
            return dc1.mesh.materialId < dc2.mesh.materialId;
        });
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
