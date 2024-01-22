#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include <webgpu/webgpu_cpp.h>
#include <webgpu/webgpu_glfw.h>

#include <dawn/dawn_proc.h>
#include <dawn/native/DawnNative.h>

#include <GLFW/glfw3.h>

namespace
{
constexpr std::int32_t SCREEN_WIDTH = 640;
constexpr std::int32_t SCREEN_HEIGHT = 480;
}

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
                                    void* userdata) {
        if (status == (WGPURequestAdapterStatus)wgpu::RequestAdapterStatus::Success) {
            *static_cast<wgpu::Adapter*>(userdata) = wgpu::Adapter::Acquire(adapter);
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
    auto onDeviceRequestEnded =
        [](WGPURequestDeviceStatus status, WGPUDevice device, char const* message, void* userdata) {
            if (status == (WGPURequestDeviceStatus)wgpu::RequestDeviceStatus::Success) {
                *static_cast<wgpu::Device*>(userdata) = wgpu::Device::Acquire(device);
            } else {
                std::cout << "Could not get WebGPU device: " << message << std::endl;
            }
        };

    wgpu::Device device;
    adapter.RequestDevice(descriptor, onDeviceRequestEnded, (void*)&device);
    return device;
}

}

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

const char* shaderSource = R"(
struct VertexInput {
    @location(0) position: vec2f,
    @location(1) color: vec3f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec3f,
};

struct PerFrameData {
    tintColor: vec4f,
};

@group(0) @binding(0) var<uniform> fd: PerFrameData;
@group(0) @binding(1) var<uniform> fd2: PerFrameData;

@group(1) @binding(0) var<uniform> fd3: PerFrameData;
@group(1) @binding(1) var<uniform> fd4: PerFrameData;

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    out.position = vec4f(in.position, 0.0, 1.0);
    out.color = in.color * fd.tintColor.rgb;

    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let color = in.color;
    return vec4f(color, 1.0);
}
)";

/* clang-format off */
std::vector<float> vertexData = {
    // x    y    r    g    b
    -0.5, -0.5, 1.0, 0.0, 0.0, //
    +0.5, -0.5, 0.0, 1.0, 0.0, //
    +0.0, +0.5, 0.0, 0.0, 1.0, //
};
/* clang-format on */

static void glfwErrorCallback(int error, const char* description)
{
    fprintf(stderr, "Error: %s\n", description);
}
} // end of anonymous namespace

struct PerFrameData {
    float tintColor[4];
};

class App {
public:
    void start();

private:
    void init();
    void createPipeline();

    void loop();
    void render();
    void cleanup();

    GLFWwindow* window;

    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Queue queue;
    std::unique_ptr<wgpu::Surface> surface;

    std::unique_ptr<wgpu::SwapChain> swapChain;
    wgpu::TextureFormat swapChainFormat;

    wgpu::ShaderModule shaderModule;
    wgpu::RenderPipeline pipeline;
    wgpu::Buffer vertexBuffer;

    wgpu::Buffer perFrameDataBuffer;
    wgpu::Buffer perFrameDataBuffer2;
    wgpu::Buffer perFrameDataBuffer3;

    wgpu::BindGroup bindGroup;
    wgpu::BindGroup secondBindGroup;
};

void App::start()
{
    init();
    loop();
    cleanup();
}

void App::init()
{
    // Initialize GLFW
    if (!glfwInit()) {
        std::exit(1);
    }
    glfwSetErrorCallback(glfwErrorCallback);

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

    window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "WebGPU", NULL, NULL);
    if (!window) {
        glfwTerminate();
        std::exit(1);
    }

    util::initWebGPU();

    const auto instanceDesc = wgpu::InstanceDescriptor{};
    instance = wgpu::CreateInstance(&instanceDesc);
    if (!instance) {
        std::cerr << "Could not initialize WebGPU!\n";
        std::exit(1);
    }

    const auto adapterOpts = wgpu::RequestAdapterOptions{};
    adapter = util::requestAdapter(instance, &adapterOpts);

    surface = std::make_unique<wgpu::Surface>(wgpu::glfw::CreateSurfaceForWindow(instance, window));

    const auto requiredLimits = wgpu::RequiredLimits{};
    const auto deviceDesc = wgpu::DeviceDescriptor{
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

    createPipeline();
}

namespace
{
wgpu::Buffer createDataBuffer(const wgpu::Device& device, const wgpu::Queue& queue)
{
    const auto bufferDesc = wgpu::BufferDescriptor{
        .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
        .size = sizeof(PerFrameData),
    };

    auto buffer = device.CreateBuffer(&bufferDesc);

    const auto pfd = PerFrameData{
        .tintColor = {0.5f, 0.1f, 0.2f, 1.0f},
    };
    queue.WriteBuffer(buffer, 0, &pfd, sizeof(buffer));

    return buffer;
}

}

void App::createPipeline()
{
    perFrameDataBuffer = createDataBuffer(device, queue);
    perFrameDataBuffer2 = createDataBuffer(device, queue);
    perFrameDataBuffer3 = createDataBuffer(device, queue);

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
            .size = vertexData.size() * sizeof(float),
        };

        vertexBuffer = device.CreateBuffer(&bufferDesc);
        queue.WriteBuffer(vertexBuffer, 0, vertexData.data(), bufferDesc.size);
    }

    // per frame data
    wgpu::BindGroupLayout perFrameDataGroupLayout;
    {
        std::array<wgpu::BindGroupLayoutEntry, 2> entries{{
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
                .visibility = wgpu::ShaderStage::Vertex,
                .buffer =
                    {
                        .type = wgpu::BufferBindingType::Uniform,
                    },
            },
        }};

        const auto bindGroupLayoutDesc = wgpu::BindGroupLayoutDescriptor{
            .entryCount = entries.size(),
            .entries = entries.data(),
        };
        perFrameDataGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDesc);

        // ISSUEMARK1: replace "2" with "3" below
        // const std::array<wgpu::BindGroupEntry, 3> bindings{{
        const std::array<wgpu::BindGroupEntry, 2> bindings{{
            {
                .binding = 0,
                .buffer = perFrameDataBuffer,
            },
            {
                .binding = 1,
                .buffer = perFrameDataBuffer2,
            },
        }};
        const auto bindGroupDesc = wgpu::BindGroupDescriptor{
            .layout = perFrameDataGroupLayout.Get(),
            .entryCount = bindings.size(),
            .entries = bindings.data(),
        };

        bindGroup = device.CreateBindGroup(&bindGroupDesc);
    }

    wgpu::BindGroupLayout secondPerFrameDataGroupLayout;
    {
        std::array<wgpu::BindGroupLayoutEntry, 1> entries{{
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
            .entryCount = entries.size(),
            .entries = entries.data(),
        };
        secondPerFrameDataGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDesc);

        const std::array<wgpu::BindGroupEntry, 1> bindings{{
            {
                .binding = 0,
                .buffer = perFrameDataBuffer3,
            },
        }};
        const auto bindGroupDesc = wgpu::BindGroupDescriptor{
            // ISSUEMARK2: replace "secondPerFrameDataGroupLayout" with perFrameDataGroupLayout
            .layout = secondPerFrameDataGroupLayout.Get(),
            // .layout = perFrameDataGroupLayout.Get(),
            .entryCount = bindings.size(),
            .entries = bindings.data(),
        };

        secondBindGroup = device.CreateBindGroup(&bindGroupDesc);
    }

    { // pipeline
        std::array<wgpu::BindGroupLayout, 2> groupLayouts{
            perFrameDataGroupLayout,
            secondPerFrameDataGroupLayout,
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
            .attributeCount = vertexAttribs.size(),
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

void App::loop()
{
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        device.PushErrorScope(wgpu::ErrorFilter::Validation);
        render();
        device.PopErrorScope(defaultValidationErrorHandler, nullptr);

        instance.ProcessEvents();
    }
}

void App::render()
{
    // cornflower blue <3
    static const wgpu::Color clearColor{100.f / 255.f, 149.f / 255.f, 237.f / 255.f, 255.f / 255.f};

    const auto frameTexture = swapChain->GetCurrentTextureView();
    if (!frameTexture) {
        std::cerr << "Cannot acquire next swap chain texture" << std::endl;
        std::exit(1);
    }

    const auto commandEncoderDesc = wgpu::CommandEncoderDescriptor{};
    const auto encoder = device.CreateCommandEncoder(&commandEncoderDesc);

    const auto mainScreenAttachment = wgpu::RenderPassColorAttachment{
        .view = frameTexture,
        .loadOp = wgpu::LoadOp::Clear,
        .storeOp = wgpu::StoreOp::Store,
        .clearValue = clearColor,
    };

    const auto renderPassDesc = wgpu::RenderPassDescriptor{
        .colorAttachmentCount = 1,
        .colorAttachments = &mainScreenAttachment,
    };

    const auto renderPass = encoder.BeginRenderPass(&renderPassDesc);

    // draw triangle
    renderPass.SetPipeline(pipeline);
    renderPass.SetVertexBuffer(0, vertexBuffer, 0, vertexData.size() * sizeof(float));
    renderPass.SetBindGroup(0, bindGroup, 0, 0); // ISSUEMARK3: comment out this line
    renderPass.SetBindGroup(1, secondBindGroup, 0, 0);
    renderPass.Draw(3, 1, 0, 0);

    renderPass.End();

    // submit
    const auto cmdBufferDesc = wgpu::CommandBufferDescriptor{};
    const auto command = encoder.Finish(&cmdBufferDesc);
    queue.Submit(1, &command);

    // flush
    swapChain->Present();
}

void App::cleanup()
{
    swapChain.reset();
    surface.reset();

    // destroy device before window
    auto d = device.MoveToCHandle();
    wgpuDeviceDestroy(d);

    glfwDestroyWindow(window);
    glfwTerminate();
}

int main()
{
    App app;
    app.start();
}
