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

@group(0) @binding(0) var texture: texture_2d<f32>;

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    out.position = vec4f(in.position, 1.0);
    out.uv = in.uv;
    return out;
}

@fragment

fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // uv has origin at bottom left
    var uv_c = in.uv;
    // uv_c.y = 1 - uv_c.y;

    let coord = vec2i(uv_c * vec2f(textureDimensions(texture)));
    let textureColor = textureLoad(texture, coord, 0).rgb;
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

    let coord = vec2i(uv_c * vec2f(textureDimensions(texture)));
    let textureColor = textureLoad(texture, coord, 0).rgb;
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

    wgpu::InstanceDescriptor instanceDesc{};
    instance = wgpu::CreateInstance(&instanceDesc);
    if (!instance) {
        std::cerr << "Could not initialize WebGPU!\n";
        std::exit(1);
    }

    wgpu::RequestAdapterOptions adapterOpts{};
    adapter = util::requestAdapter(instance, &adapterOpts);

    wgpu::SupportedLimits supportedLimits{};
    adapter.GetLimits(&supportedLimits);

    wgpu::RequiredLimits requiredLimits{
        .limits =
            {
                // .minUniformBufferOffsetAlignment = 256,
                // .minStorageBufferOffsetAlignment =
                //     supportedLimits.limits.minStorageBufferOffsetAlignment,
                // .maxVertexAttributes = 8,
                // .maxInterStageShaderComponents = 3,
            },
    };

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

    surface = SDL_GetWGPUSurface(instance, window);

    wgpu::DeviceDescriptor deviceDesc{
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
            std::make_unique<wgpu::SwapChain>(device.CreateSwapChain(surface, &swapChainDesc));
    }

    initModelStuff();
    initSpriteStuff();
}

void Game::initModelStuff()
{
    { // create shader module
        wgpu::ShaderModuleWGSLDescriptor shaderCodeDesc;
        shaderCodeDesc.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
        shaderCodeDesc.code = shaderSource;

        wgpu::ShaderModuleDescriptor shaderDesc{};
        shaderDesc.nextInChain = reinterpret_cast<wgpu::ChainedStruct*>(&shaderCodeDesc);

        shaderModule = device.CreateShaderModule(&shaderDesc);
    }

    // load model
    model = util::loadModel("assets/models/yae.gltf");
    // let's assume one mesh for now
    assert(model.meshes.size() == 1);
    auto& mesh = model.meshes[0];
    std::cout << "num vertices:" << mesh.vertices.size() << std::endl;

    { // load diffuse texture
        ImageData data = util::loadImage(mesh.diffuseTexturePath);
        assert(data.pixels != nullptr);
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

    { // create bind group
        const std::vector<wgpu::BindGroupLayoutEntry> bindingLayoutEntries{
            {
                .binding = 0,
                .visibility = wgpu::ShaderStage::Fragment,
                .texture =
                    {
                        .sampleType = wgpu::TextureSampleType::Float,
                        .viewDimension = wgpu::TextureViewDimension::e2D,
                    },
            },
        };

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

        const std::vector<wgpu::BindGroupEntry> bindings{{
            .binding = 0,
            .textureView = textureView,
        }};
        wgpu::BindGroupDescriptor bindGroupDesc{
            .layout = bindGroupLayout.Get(),
            .entryCount = bindings.size(),
            .entries = bindings.data(),
        };

        bindGroup = device.CreateBindGroup(&bindGroupDesc);
    }

    { // vertex buffer
        wgpu::BufferDescriptor bufferDesc{
            .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Vertex,
            .size = mesh.vertices.size() * sizeof(Mesh::Vertex),
            .mappedAtCreation = false,
        };

        vertexBuffer = device.CreateBuffer(&bufferDesc);

        queue.WriteBuffer(vertexBuffer, 0, mesh.vertices.data(), bufferDesc.size);
    }

    { // index buffer
        // TEMP HACK: for some reason I can only draw even amount of triangles...
        const auto oldIndices = mesh.indices;
        mesh.indices.clear();
        for (int i = 0; i < 3 * 446; ++i) {
            mesh.indices.push_back(oldIndices[i]);
        }

        wgpu::BufferDescriptor bufferDesc{
            .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Index,
            .size = mesh.indices.size() * sizeof(std::uint16_t),
            .mappedAtCreation = false,
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

        // vertex
        const std::vector<wgpu::VertexAttribute> vertexAttribs{
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
        };

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

        wgpu::ColorTargetState colorTarget = {
            .format = swapChainFormat,
            .blend = &blendState,
            .writeMask = wgpu::ColorWriteMask::All,
        };

        wgpu::FragmentState fragmentState = {
            .module = shaderModule,
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

        pipeline = device.CreateRenderPipeline(&pipelineDesc);
    }
}

void Game::initSpriteStuff()
{
    { // create sprite shader module
        wgpu::ShaderModuleWGSLDescriptor shaderCodeDesc;
        shaderCodeDesc.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
        shaderCodeDesc.code = spriteShaderSource;

        wgpu::ShaderModuleDescriptor shaderDesc{};
        shaderDesc.nextInChain = reinterpret_cast<wgpu::ChainedStruct*>(&shaderCodeDesc);

        spriteShaderModule = device.CreateShaderModule(&shaderDesc);
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

    {
        const std::vector<wgpu::BindGroupLayoutEntry> bindingLayoutEntries{
            {
                .binding = 0,
                .visibility = wgpu::ShaderStage::Fragment,
                .texture =
                    {
                        .sampleType = wgpu::TextureSampleType::Float,
                        .viewDimension = wgpu::TextureViewDimension::e2D,
                    },
            },
        };

        const wgpu::BindGroupLayoutDescriptor bindGroupLayoutDesc{
            .entryCount = bindingLayoutEntries.size(),
            .entries = bindingLayoutEntries.data(),
        };
        spriteBindGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDesc);

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

        const std::vector<wgpu::BindGroupEntry> bindings{{
            .binding = 0,
            .textureView = textureView,
        }};
        wgpu::BindGroupDescriptor bindGroupDesc{
            .layout = spriteBindGroupLayout.Get(),
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
        const std::vector<wgpu::VertexAttribute> vertexAttribs{
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
        };

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

        wgpu::ColorTargetState colorTarget = {
            .format = swapChainFormat,
            .blend = &blendState,
            .writeMask = wgpu::ColorWriteMask::All,
        };

        wgpu::FragmentState fragmentState = {
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
            .bindGroupLayouts = &spriteBindGroupLayout,
        };
        pipelineDesc.layout = device.CreatePipelineLayout(&layoutDesc);

        spritePipeline = device.CreateRenderPipeline(&pipelineDesc);
    }

    { // vertex buffer
        wgpu::BufferDescriptor bufferDesc{
            .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Vertex,
            .size = pointData.size() * sizeof(float),
            .mappedAtCreation = false,
        };

        spriteVertexBuffer = device.CreateBuffer(&bufferDesc);

        queue.WriteBuffer(spriteVertexBuffer, 0, pointData.data(), bufferDesc.size);
    }

    { // index buffer
        wgpu::BufferDescriptor bufferDesc{
            .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Index,
            .size = indexData.size() * sizeof(std::uint16_t),
            .mappedAtCreation = false,
        };

        spriteIndexBuffer = device.CreateBuffer(&bufferDesc);

        queue.WriteBuffer(spriteIndexBuffer, 0, indexData.data(), bufferDesc.size);
    }
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
{}

void Game::render()
{
    // cornflower blue <3
    static const wgpu::Color clearColor{100.f / 255.f, 149.f / 255.f, 237.f / 255.f, 255.f / 255.f};

    auto nextFrameTexture = swapChain->GetCurrentTextureView();
    if (!nextFrameTexture) {
        std::cerr << "Cannot acquire next swap chain texture" << std::endl;
        return;
    }

    wgpu::CommandEncoderDescriptor commandEncoderDesc{};
    const auto encoder = device.CreateCommandEncoder(&commandEncoderDesc);

    const wgpu::RenderPassColorAttachment renderPassColorAttachment = {
        .view = nextFrameTexture,
        .loadOp = wgpu::LoadOp::Clear,
        .storeOp = wgpu::StoreOp::Store,
        .clearValue = clearColor,
    };

    const wgpu::RenderPassDescriptor renderPassDesc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &renderPassColorAttachment,
    };

    const auto renderPass = encoder.BeginRenderPass(&renderPassDesc);

    { // draw sprite
        renderPass.SetPipeline(spritePipeline);
        renderPass.SetBindGroup(0, spriteBindGroup, 0, nullptr);
        renderPass.SetVertexBuffer(0, spriteVertexBuffer, 0, pointData.size() * sizeof(float));
        renderPass.SetIndexBuffer(
            spriteIndexBuffer, wgpu::IndexFormat::Uint16, 0, indexCount * sizeof(std::uint16_t));
        renderPass.DrawIndexed(indexCount, 1, 0, 0, 0);
    }

    { // draw mesh
        auto& mesh = model.meshes[0];

        renderPass.SetPipeline(pipeline);
        renderPass.SetBindGroup(0, bindGroup, 0, nullptr);
        renderPass.SetVertexBuffer(0, vertexBuffer, 0, mesh.vertices.size() * sizeof(Mesh::Vertex));
        renderPass.SetIndexBuffer(
            indexBuffer, wgpu::IndexFormat::Uint16, 0, mesh.indices.size() * sizeof(std::uint16_t));
        renderPass.DrawIndexed(mesh.indices.size(), 1, 0, 0, 0);
    }

    renderPass.End();

    // submit
    wgpu::CommandBufferDescriptor cmdBufferDescriptor{};
    const auto command = encoder.Finish(&cmdBufferDescriptor);
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
    swapChain.reset();
    SDL_DestroyWindow(window);

    SDL_Quit();
}
