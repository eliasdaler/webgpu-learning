#include "Game.h"

#include "util/ImageLoader.h"
#include "util/OSUtil.h"
#include "util/SDLWebGPU.h"
#include "util/WebGPUUtil.h"

#include <SDL.h>

#include <cstdint>
#include <webgpu/webgpu.h>

#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>

namespace
{
const char* shaderSource = R"(
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
    0,
    2,
    3 // Triangle #1
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

    WGPUInstanceDescriptor instanceDesc{};
    instance = wgpuCreateInstance(&instanceDesc);
    if (!instance) {
        std::cerr << "Could not initialize WebGPU!\n";
        std::exit(1);
    }

    WGPURequestAdapterOptions adapterOpts{};
    adapter = util::requestAdapter(instance, &adapterOpts);

    WGPUSupportedLimits supportedLimits{};
    wgpuAdapterGetLimits(adapter, &supportedLimits);

    WGPURequiredLimits requiredLimits{
        .limits =
            {
                .minUniformBufferOffsetAlignment = 256,
                .minStorageBufferOffsetAlignment =
                    supportedLimits.limits.minStorageBufferOffsetAlignment,
                .maxVertexAttributes = 2,
                .maxInterStageShaderComponents = 3,
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

    WGPUSurface surface = SDL_GetWGPUSurface(instance, window);

    WGPUDeviceDescriptor deviceDesc{
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
    wgpuDeviceSetUncapturedErrorCallback(device, onDeviceError, nullptr);

    wgpuDeviceSetDeviceLostCallback(
        device,
        [](WGPUDeviceLostReason reason, char const* message, void* userdata) {
            // std::cout << "WGPU device lost" << std::endl;
        },
        nullptr);

    queue = wgpuDeviceGetQueue(device);
    auto onQueueWorkDone = [](WGPUQueueWorkDoneStatus status, void* /* pUserData */) {
        // std::cout << "Queued work finished with status: " << status << std::endl;
    };
    wgpuQueueOnSubmittedWorkDone(queue, onQueueWorkDone, nullptr);

    { // init swapchain
        swapChainFormat = WGPUTextureFormat_BGRA8Unorm;

        const WGPUSwapChainDescriptor swapChainDesc = {
            .usage = WGPUTextureUsage_RenderAttachment,
            .format = swapChainFormat,
            .width = static_cast<std::uint32_t>(params.screenWidth),
            .height = static_cast<std::uint32_t>(params.screenHeight),
            .presentMode = WGPUPresentMode_Fifo,
        };

        swapChain = wgpuDeviceCreateSwapChain(device, surface, &swapChainDesc);
    }

    {
        const WGPUShaderModuleWGSLDescriptor shaderCodeDesc = {
            .chain = {.sType = WGPUSType_ShaderModuleWGSLDescriptor},
            .code = shaderSource,
        };

        WGPUShaderModuleDescriptor shaderDesc{};
        shaderDesc.nextInChain = &shaderCodeDesc.chain;

        shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);
    }

    {
        ImageData data = util::loadImage("assets/textures/shinji.png");
        assert(data.channels == 4);

        const WGPUTextureDescriptor textureDesc{
            .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
            .dimension = WGPUTextureDimension_2D,
            .size =
                {
                    .width = static_cast<std::uint32_t>(data.width),
                    .height = static_cast<std::uint32_t>(data.height),
                    .depthOrArrayLayers = 1,
                },
            .format = WGPUTextureFormat_RGBA8UnormSrgb,
            .mipLevelCount = 1,
            .sampleCount = 1,
        };
        texture = wgpuDeviceCreateTexture(device, &textureDesc);

        const WGPUImageCopyTexture destination{
            .texture = texture,
            .mipLevel = 0,
            .origin = {0, 0, 0},
        };
        const WGPUTextureDataLayout source{
            .bytesPerRow = static_cast<std::uint32_t>(data.width * data.channels),
            .rowsPerImage = static_cast<std::uint32_t>(data.height)};

        const auto pixelsSize = data.width * data.height * data.channels;
        wgpuQueueWriteTexture(
            queue, &destination, (void*)data.pixels, pixelsSize, &source, &textureDesc.size);
    }

    {
        const std::vector<WGPUBindGroupLayoutEntry> bindingLayoutEntries{
            {
                .binding = 0,
                .visibility = WGPUShaderStage_Fragment,
                .texture =
                    {
                        .sampleType = WGPUTextureSampleType_Float,
                        .viewDimension = WGPUTextureViewDimension_2D,
                    },
            },
        };

        const WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc{
            .entryCount = bindingLayoutEntries.size(),
            .entries = bindingLayoutEntries.data(),
        };
        bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDesc);

        const WGPUTextureViewDescriptor textureViewDesc{
            .format = WGPUTextureFormat_RGBA8UnormSrgb,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = 0,
            .mipLevelCount = 1,
            .baseArrayLayer = 0,
            .arrayLayerCount = 1,
            .aspect = WGPUTextureAspect_All,
        };
        const auto textureView = wgpuTextureCreateView(texture, &textureViewDesc);

        const std::vector<WGPUBindGroupEntry> bindings{{
            .binding = 0,
            .textureView = textureView,
        }};
        WGPUBindGroupDescriptor bindGroupDesc{
            .layout = bindGroupLayout,
            .entryCount = bindings.size(),
            .entries = bindings.data(),
        };

        bindGroup = wgpuDeviceCreateBindGroup(device, &bindGroupDesc);
    }

    {
        WGPURenderPipelineDescriptor pipelineDesc{};

        pipelineDesc.primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .stripIndexFormat = WGPUIndexFormat_Undefined,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = WGPUCullMode_None,
        };

        // vertex
        const std::vector<WGPUVertexAttribute> vertexAttribs{
            {
                // position
                .format = WGPUVertexFormat_Float32x2,
                .offset = 0,
                .shaderLocation = 0,
            },
            {
                // uv
                .format = WGPUVertexFormat_Float32x2,
                .offset = 2 * sizeof(float),
                .shaderLocation = 1,
            },
        };

        const WGPUVertexBufferLayout vertexBufferLayout{
            .arrayStride = 4 * sizeof(float),
            .stepMode = WGPUVertexStepMode_Vertex,
            .attributeCount = static_cast<std::size_t>(vertexAttribs.size()),
            .attributes = vertexAttribs.data(),
        };

        pipelineDesc.vertex = {
            .module = shaderModule,
            .entryPoint = "vs_main",
            .bufferCount = 1,
            .buffers = &vertexBufferLayout,
        };

        // fragment
        const WGPUBlendState blendState{
            .color =
                {
                    .operation = WGPUBlendOperation_Add,
                    .srcFactor = WGPUBlendFactor_SrcAlpha,
                    .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
                },
            .alpha = {
                .operation = WGPUBlendOperation_Add,
                .srcFactor = WGPUBlendFactor_Zero,
                .dstFactor = WGPUBlendFactor_One,
            }};

        WGPUColorTargetState colorTarget = {
            .format = swapChainFormat,
            .blend = &blendState,
            .writeMask = WGPUColorWriteMask_All,
        };

        WGPUFragmentState fragmentState = {
            .module = shaderModule,
            .entryPoint = "fs_main",
            .targetCount = 1,
            .targets = &colorTarget,
        };
        pipelineDesc.fragment = &fragmentState;

        pipelineDesc.multisample.count = 1;
        pipelineDesc.multisample.mask = ~0u;
        pipelineDesc.multisample.alphaToCoverageEnabled = false;

        WGPUPipelineLayoutDescriptor layoutDesc{
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &bindGroupLayout,
        };
        pipelineDesc.layout = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);

        pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
    }

    {
        WGPUBufferDescriptor bufferDesc{
            .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex,
            .size = pointData.size() * sizeof(float),
            .mappedAtCreation = false,
        };

        vertexBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);

        wgpuQueueWriteBuffer(queue, vertexBuffer, 0, pointData.data(), bufferDesc.size);
    }

    {
        WGPUBufferDescriptor bufferDesc{
            .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index,
            .size = indexData.size() * sizeof(std::uint16_t),
            .mappedAtCreation = false,
        };

        indexBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);

        wgpuQueueWriteBuffer(queue, indexBuffer, 0, indexData.data(), bufferDesc.size);
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
    static const WGPUColor clearColor{100.f / 255.f, 149.f / 255.f, 237.f / 255.f, 255.f / 255.f};

    WGPUTextureView nextFrameTexture = wgpuSwapChainGetCurrentTextureView(swapChain);
    if (!nextFrameTexture) {
        std::cerr << "Cannot acquire next swap chain texture" << std::endl;
        return;
    }

    WGPUCommandEncoderDescriptor commandEncoderDesc{};
    const auto encoder = wgpuDeviceCreateCommandEncoder(device, &commandEncoderDesc);

    const WGPURenderPassColorAttachment renderPassColorAttachment = {
        .view = nextFrameTexture,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = clearColor,
    };

    const WGPURenderPassDescriptor renderPassDesc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &renderPassColorAttachment,
    };

    const auto renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    { // finally, do drawing
        wgpuRenderPassEncoderSetPipeline(renderPass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(renderPass, 0, bindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(
            renderPass, 0, vertexBuffer, 0, pointData.size() * sizeof(float));
        wgpuRenderPassEncoderSetIndexBuffer(
            renderPass, indexBuffer, WGPUIndexFormat_Uint16, 0, indexCount * sizeof(std::uint16_t));
        wgpuRenderPassEncoderDrawIndexed(renderPass, indexCount, 1, 0, 0, 0);
    }

    wgpuRenderPassEncoderEnd(renderPass);

    wgpuTextureViewRelease(nextFrameTexture);

    // submit
    WGPUCommandBufferDescriptor cmdBufferDescriptor{};
    const auto command = wgpuCommandEncoderFinish(encoder, &cmdBufferDescriptor);
    wgpuQueueSubmit(queue, 1, &command);

    // flush
    wgpuSwapChainPresent(swapChain);
}

void Game::quit()
{
    isRunning = false;
}

void Game::cleanup()
{
    wgpuSwapChainRelease(swapChain);
    wgpuDeviceRelease(device);
    wgpuAdapterRelease(adapter);
    wgpuInstanceRelease(instance);

    SDL_DestroyWindow(window);

    SDL_Quit();
}
