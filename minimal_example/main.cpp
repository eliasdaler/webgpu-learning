#pragma once

#include <memory>

#include <webgpu/webgpu_cpp.h>

#include <dawn/dawn_proc.h>
#include <dawn/native/DawnNative.h>

#include <iostream>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <cstdint>
#include <webgpu/webgpu_cpp.h>

#include <iostream>
#include <vector>

// X11
#undef None

namespace
{
constexpr std::int32_t SCREEN_WIDTH = 640;
constexpr std::int32_t SCREEN_HEIGHT = 480;
}

struct SDL_Window;

namespace util
{
void initWebGPU()
{
    dawnProcSetProcs(&dawn::native::GetProcs());
}

wgpu::Adapter requestAdapter(
    const wgpu::Instance& instance,
    wgpu::RequestAdapterOptions const* options)
{
    auto onAdapterRequestEnded = [](WGPURequestAdapterStatus status,
                                    WGPUAdapter adapter,
                                    char const* message,
                                    void* pUserData) {
        if (status == WGPURequestAdapterStatus_Success) {
            // std::cout << "Got WebGPU adapter" << std::endl;
            *static_cast<wgpu::Adapter*>(pUserData) = wgpu::Adapter::Acquire(adapter);
        } else {
            std::cout << "Could not get WebGPU adapter: " << message << std::endl;
        }
    };

    wgpu::Adapter adapter;
    instance.RequestAdapter(options, onAdapterRequestEnded, (void*)&adapter);
    return adapter;
}

wgpu::Device requestDevice(const wgpu::Adapter& adapter, wgpu::DeviceDescriptor const* descriptor)
{
    auto onDeviceRequestEnded = [](WGPURequestDeviceStatus status,
                                   WGPUDevice device,
                                   char const* message,
                                   void* pUserData) {
        if (status == WGPURequestDeviceStatus_Success) {
            *static_cast<wgpu::Device*>(pUserData) = wgpu::Device::Acquire(device);
        } else {
            std::cout << "Could not get WebGPU device: " << message << std::endl;
        }
    };

    wgpu::Device device;
    adapter.RequestDevice(descriptor, onDeviceRequestEnded, (void*)&device);
    return device;
}

wgpu::Surface SDL_GetWGPUSurface(const wgpu::Instance& instance, SDL_Window* window)
{
    SDL_SysWMinfo windowWMInfo;
    SDL_VERSION(&windowWMInfo.version);
    SDL_GetWindowWMInfo(window, &windowWMInfo);

#if defined(SDL_VIDEO_DRIVER_X11)
    {
        Display* x11_display = windowWMInfo.info.x11.display;
        Window x11_window = windowWMInfo.info.x11.window;

        wgpu::SurfaceDescriptorFromXlibWindow x11SurfDesc;
        x11SurfDesc.display = x11_display;
        x11SurfDesc.window = x11_window;

        wgpu::SurfaceDescriptor surfaceDesc;
        surfaceDesc.nextInChain = &x11SurfDesc;
        return instance.CreateSurface(&surfaceDesc);
    }
#elif defined(SDL_VIDEO_DRIVER_WINDOWS)
    static_assert(false, "untested");
    /* {
        HWND hwnd = windowWMInfo.info.win.window;
        HINSTANCE hinstance = GetModuleHandle(NULL);

        WGPUSurfaceDescriptorFromWindowsHWND windowDesc{
            .chain =
                WGPUChainedStruct{
                    .next = NULL,
                    .sType = WGPUSType_SurfaceDescriptorFromWindowsHWND,
                },
            .hinstance = hinstance,
            .hwnd = hwnd,
        };
        WGPUSurfaceDescriptor surfaceDesc{
            .nextInChain = (const WGPUChainedStruct*)&windowDesc,
            .label = NULL,
        };
        return wgpuInstanceCreateSurface(instance, &surfaceDesc);
    } */
#else
    // TODO: See SDL_syswm.h for other possible enum values!
#error "Unsupported WGPU_TARGET"
#endif
}

}

class Game {
public:
    void start();

private:
    void init();
    void initModelStuff();
    void initSpriteStuff();
    void initCamera();

    void loop();
    void update(float dt);
    void render();
    void quit();
    void cleanup();

    bool isRunning{false};

    SDL_Window* window{nullptr};

    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;

    std::unique_ptr<wgpu::Surface> surface;
    std::unique_ptr<wgpu::SwapChain> swapChain;

    wgpu::TextureFormat swapChainFormat;
    wgpu::Queue queue;

    wgpu::ShaderModule shaderModule;

    wgpu::RenderPipeline pipeline;

    wgpu::Buffer vertexBuffer;
};

namespace
{
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
    @location(0) position: vec2f,
    @location(1) color: vec3f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec3f,
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    out.position = vec4f(in.position, 0.0, 1.0);
    out.color = in.color;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    return vec4f(in.color, 1.0);
}
)";

/* clang-format off */
std::vector<float> pointData = {
    // x0,  y0,  r0,  g0,  b0
    -0.5, -0.5, 1.0, 0.0, 0.0, //

    // x1,  y1,  r1,  g1,  b1
    +0.5, -0.5, 0.0, 1.0, 0.0, //

    // ...
    +0.0, +0.5, 0.0, 0.0, 1.0, //
};
/* clang-format on */

} // end of anonymous namespace

void Game::start()
{
    init();
    loop();
    cleanup();
}

void Game::init()
{
    util::initWebGPU();

    const auto instanceDesc = wgpu::InstanceDescriptor{};
    instance = wgpu::CreateInstance(&instanceDesc);
    if (!instance) {
        std::cerr << "Could not initialize WebGPU!\n";
        std::exit(1);
    }

    const auto adapterOpts = wgpu::RequestAdapterOptions{};
    adapter = util::requestAdapter(instance, &adapterOpts);

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        printf("SDL could not initialize! SDL Error: %s\n", SDL_GetError());
        std::exit(1);
    }

    window = SDL_CreateWindow(
        "WebGPU demo",
        // pos
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        // size
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        0);

    if (!window) {
        std::cout << "Failed to create window. SDL Error: " << SDL_GetError();
        std::exit(1);
    }

    surface = std::make_unique<wgpu::Surface>(util::SDL_GetWGPUSurface(instance, window));

    // TODO: set limits
    const auto requiredLimits = wgpu::RequiredLimits{};
    const auto deviceDesc = wgpu::DeviceDescriptor{
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

    queue = device.GetQueue();

    swapChainFormat = wgpu::TextureFormat::BGRA8Unorm;
    { // init swapchain
        const auto swapChainDesc = wgpu::SwapChainDescriptor{
            .usage = wgpu::TextureUsage::RenderAttachment,
            .format = swapChainFormat,
            .width = SCREEN_WIDTH,
            .height = SCREEN_HEIGHT,
            .presentMode = wgpu::PresentMode::Fifo,
        };

        swapChain =
            std::make_unique<wgpu::SwapChain>(device.CreateSwapChain(*surface, &swapChainDesc));
    }

    initModelStuff();
}

void Game::initModelStuff()
{
    { // create shader module
        auto shaderCodeDesc = wgpu::ShaderModuleWGSLDescriptor{};
        shaderCodeDesc.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
        shaderCodeDesc.code = shaderSource;

        const auto shaderDesc = wgpu::ShaderModuleDescriptor{
            .nextInChain = reinterpret_cast<wgpu::ChainedStruct*>(&shaderCodeDesc),
            .label = "shader",
        };

        shaderModule = device.CreateShaderModule(&shaderDesc);
        shaderModule.GetCompilationInfo(defaultCompilationCallback, (void*)"shader");
    }

    { // vertex buffer
        const auto bufferDesc = wgpu::BufferDescriptor{
            .usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst,
            .size = pointData.size() * sizeof(float),
        };

        vertexBuffer = device.CreateBuffer(&bufferDesc);
        queue.WriteBuffer(vertexBuffer, 0, pointData.data(), bufferDesc.size);
    }

    {
        wgpu::RenderPipelineDescriptor pipelineDesc{
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
                // color
                .format = wgpu::VertexFormat::Float32x3,
                .offset = 2 * sizeof(float),
                .shaderLocation = 1,
            },
        }};

        const auto vertexBufferLayout = wgpu::VertexBufferLayout{
            .arrayStride = 5 * sizeof(float),
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
            .module = shaderModule,
            .entryPoint = "fs_main",
            .targetCount = 1,
            .targets = &colorTarget,
        };
        pipelineDesc.fragment = &fragmentState;

        pipeline = device.CreateRenderPipeline(&pipelineDesc);
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
        renderPass.PushDebugGroup("Draw");

        { // draw triangles
            renderPass.SetPipeline(pipeline);
            renderPass.SetVertexBuffer(0, vertexBuffer, 0, pointData.size() * sizeof(float));
            renderPass.Draw(3, 1, 0, 0);
        }

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
    swapChain.reset();
    surface.reset();

    // need to destroy the device so that the app doesn't crash on SDL_Quit
    // kinda hacky, but works
    auto d = device.MoveToCHandle();
    wgpuDeviceDestroy(d);

    SDL_Quit();
}

int main()
{
    Game game;
    game.start();
}
