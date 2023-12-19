#include "Game.h"

#include "util/OSUtil.h"
#include "util/SDLWebGPU.h"
#include "util/WebGPUUtil.h"

#include <SDL.h>

#include <webgpu/webgpu.h>

#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {
const char *shaderSource = R"(
struct VertexInput {
    @location(0) position: vec2f,
    @location(1) color: vec3f,
};

/**
 * A structure with fields labeled with builtins and locations can also be used
 * as *output* of the vertex shader, which is also the input of the fragment
 * shader.
 */
struct VertexOutput {
    @builtin(position) position: vec4f,
    // The location here does not refer to a vertex attribute, it just means
    // that this field must be handled by the rasterizer.
    // (It can also refer to another field of another struct that would be used
    // as input to the fragment shader.)
    @location(0) color: vec3f,
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    out.position = vec4f(in.position, 0.0, 1.0);
    out.color = in.color; // forward to the fragment shader
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    return vec4f(in.color, 1.0);
}
)";

// Vertex buffer
std::vector<float> vertexData = {
    // x0,  y0,  r0,  g0,  b0
    -0.5, -0.5, 1.0, 0.0, 0.0, //

    // x1,  y1,  r1,  g1,  b1
    +0.5, -0.5, 0.0, 1.0, 0.0, //

    // ...
    +0.0, +0.5, 0.0, 0.0, 1.0, //
};
// We now divide the vector size by 5 fields.
int vertexCount = static_cast<int>(vertexData.size() / 5);

} // namespace

void Game::Params::validate() {
  assert(screenWidth > 0);
  assert(screenHeight > 0);
}

void Game::start(Params params) {
  params.validate();
  this->params = params;

  init();
  loop();
  cleanup();
}

void Game::init() {
  util::setCurrentDirToExeDir();

  util::initWebGPU();

  WGPUInstanceDescriptor instanceDesc = {};
  instanceDesc.nextInChain = nullptr;
  instance = wgpuCreateInstance(&instanceDesc);
  if (!instance) {
    std::cerr << "Could not initialize WebGPU!\n";
    std::exit(1);
  }

  WGPURequestAdapterOptions adapterOpts = {};
  adapter = util::requestAdapter(instance, &adapterOpts);

  WGPUSupportedLimits supportedLimits{};
  supportedLimits.nextInChain = nullptr;

  wgpuAdapterGetLimits(adapter, &supportedLimits);
  std::cout << "adapter.maxVertexAttributes: "
            << supportedLimits.limits.maxVertexAttributes << std::endl;

  WGPURequiredLimits requiredLimits{};
  requiredLimits.limits.maxVertexAttributes = 2;
  // We should also tell that we use 1 vertex buffers
  requiredLimits.limits.maxVertexBuffers = 1;
  // Maximum size of a buffer is 6 vertices of 2 float each
  requiredLimits.limits.maxBufferSize = 6 * 5 * sizeof(float);
  // requiredLimits.limits.maxVertexBufferArrayStride = 5 * sizeof(float);
  // ^ doesn't work on my GPU (max is 0)
  // This must be set even if we do not use storage buffers for now
  requiredLimits.limits.minStorageBufferOffsetAlignment =
      supportedLimits.limits.minStorageBufferOffsetAlignment;
  requiredLimits.limits.minUniformBufferOffsetAlignment = 4;
  requiredLimits.limits.maxInterStageShaderComponents = 3;

  // Initialize SDL
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
    printf("SDL could not initialize! SDL Error: %s\n", SDL_GetError());
    std::exit(1);
  }

  window = SDL_CreateWindow(params.windowTitle.c_str(),
                            // pos
                            SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                            // size
                            params.screenWidth, params.screenHeight, 0);

  if (!window) {
    std::cout << "Failed to create window. SDL Error: " << SDL_GetError();
    std::exit(1);
  }

  WGPUSurface surface = SDL_GetWGPUSurface(instance, window);

  WGPUDeviceDescriptor deviceDesc;
  deviceDesc.nextInChain = nullptr;
  deviceDesc.label = "My Device";
  deviceDesc.requiredFeatureCount = 0;
  deviceDesc.requiredLimits = nullptr;
  deviceDesc.defaultQueue.nextInChain = nullptr;
  deviceDesc.defaultQueue.label = "The default queue";

  deviceDesc.requiredLimits = &requiredLimits;

  device = util::requestDevice(adapter, &deviceDesc);

  auto onDeviceError = [](WGPUErrorType type, char const *message,
                          void *userdata) {
    std::cout << "Uncaptured device error: type " << type;
    if (message)
      std::cout << " (" << message << ")";
    std::cout << std::endl;
  };
  wgpuDeviceSetUncapturedErrorCallback(device, onDeviceError, nullptr);

  wgpuDeviceSetDeviceLostCallback(
      device,
      [](WGPUDeviceLostReason reason, char const *message, void *userdata) {
        // std::cout << "WGPU device lost" << std::endl;
      },
      nullptr);

  wgpuDeviceGetLimits(device, &supportedLimits);
  std::cout << "device.maxVertexAttributes: "
            << supportedLimits.limits.maxVertexAttributes << std::endl;

  queue = wgpuDeviceGetQueue(device);
  auto onQueueWorkDone = [](WGPUQueueWorkDoneStatus status,
                            void * /* pUserData */) {
    // std::cout << "Queued work finished with status: " << status << std::endl;
  };
  wgpuQueueOnSubmittedWorkDone(queue, onQueueWorkDone, nullptr /* pUserData */);

  WGPUSwapChainDescriptor swapChainDesc = {};
  swapChainDesc.nextInChain = nullptr;
  swapChainDesc.width = params.screenWidth;
  swapChainDesc.height = params.screenHeight;

  // Dawn doesn't have wgpuSurfaceGetPreferredFormat
  /* swapChainFormat =
      wgpuSurfaceGetPreferredFormat(surface, adapter);
  swapChainDesc.format = swapChainFormat; */
  swapChainFormat = WGPUTextureFormat_BGRA8Unorm;
  swapChainDesc.format = swapChainFormat;
  swapChainDesc.usage = WGPUTextureUsage_RenderAttachment;
  swapChainDesc.presentMode = WGPUPresentMode_Fifo;

  swapChain = wgpuDeviceCreateSwapChain(device, surface, &swapChainDesc);

  {
    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = nullptr;

    // Use the extension mechanism to load a WGSL shader source code
    WGPUShaderModuleWGSLDescriptor shaderCodeDesc = {};
    // Set the chained struct's header
    shaderCodeDesc.chain.next = nullptr;
    shaderCodeDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    // Connect the chain
    shaderDesc.nextInChain = &shaderCodeDesc.chain;

    // Setup the actual payload of the shader code descriptor
    shaderCodeDesc.code = shaderSource;

    shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);
  }

  {
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.nextInChain = nullptr;

    // Vertex fetch
    // (We don't use any input buffer so far)
    pipelineDesc.vertex.bufferCount = 0;
    pipelineDesc.vertex.buffers = nullptr;

    // Vertex shader
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = "vs_main";
    pipelineDesc.vertex.constantCount = 0;
    pipelineDesc.vertex.constants = nullptr;

    // Vertex fetch
    WGPUVertexBufferLayout vertexBufferLayout{};
    // vertexBufferLayout.nextInChain = nullptr; // NOT IN DAWN
    // [...] Build vertex buffer layout

    // We now have 2 attributes
    std::vector<WGPUVertexAttribute> vertexAttribs(2);

    // Position attribute
    vertexAttribs[0].shaderLocation = 0;
    vertexAttribs[0].format = WGPUVertexFormat_Float32x2;
    vertexAttribs[0].offset = 0;

    // Color attribute
    vertexAttribs[1].shaderLocation = 1;
    vertexAttribs[1].format = WGPUVertexFormat_Float32x3; // different type!
    vertexAttribs[1].offset = 2 * sizeof(float);          // non null offset!

    vertexBufferLayout.attributeCount =
        static_cast<uint32_t>(vertexAttribs.size());
    vertexBufferLayout.attributes = vertexAttribs.data();

    vertexBufferLayout.arrayStride = 5 * sizeof(float);
    vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;

    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexBufferLayout;

    // Primitive assembly and rasterization
    // Each sequence of 3 vertices is considered as a triangle
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    // We'll see later how to specify the order in which vertices should be
    // connected. When not specified, vertices are considered sequentially.
    pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    // The face orientation is defined by assuming that when looking
    // from the front of the face, its corner vertices are enumerated
    // in the counter-clockwise (CCW) order.
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    // But the face orientation does not matter much because we do not
    // cull (i.e. "hide") the faces pointing away from us (which is often
    // used for optimization).
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;

    // Fragment shader
    WGPUFragmentState fragmentState = {};
    fragmentState.nextInChain = nullptr;
    pipelineDesc.fragment = &fragmentState;
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = "fs_main";
    fragmentState.constantCount = 0;
    fragmentState.constants = nullptr;

    // Configure blend state
    WGPUBlendState blendState;
    // Usual alpha blending for the color:
    blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.color.operation = WGPUBlendOperation_Add;
    // We leave the target alpha untouched:
    blendState.alpha.srcFactor = WGPUBlendFactor_Zero;
    blendState.alpha.dstFactor = WGPUBlendFactor_One;
    blendState.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.nextInChain = nullptr;
    colorTarget.format = swapChainFormat;
    colorTarget.blend = &blendState;
    colorTarget.writeMask =
        WGPUColorWriteMask_All; // We could write to only some of the color
                                // channels.

    // We have only one target because our render pass has only one output color
    // attachment.
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    // Depth and stencil tests are not used here
    pipelineDesc.depthStencil = nullptr;

    // Multi-sampling
    // Samples per pixel
    pipelineDesc.multisample.count = 1;
    // Default value for the mask, meaning "all bits on"
    pipelineDesc.multisample.mask = ~0u;
    // Default value as well (irrelevant for count = 1 anyways)
    pipelineDesc.multisample.alphaToCoverageEnabled = false;

    // Pipeline layout
    pipelineDesc.layout = nullptr;

    pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
  }

  {

    WGPUBufferDescriptor bufferDesc{};
    bufferDesc.nextInChain = nullptr;
    bufferDesc.size = vertexData.size() * sizeof(float);
    bufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
    bufferDesc.mappedAtCreation = false;
    vertexBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);

    // Upload geometry data to the buffer
    wgpuQueueWriteBuffer(queue, vertexBuffer, 0, vertexData.data(),
                         bufferDesc.size);
  }
}

void Game::loop() {
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

void Game::update(float dt) {}

void Game::render() {
  // cornflower blue <3
  static const WGPUColor clearColor{100.f / 255.f, 149.f / 255.f, 237.f / 255.f,
                                    255.f / 255.f};

  // Get the texture where to draw the next frame
  WGPUTextureView nextTexture = wgpuSwapChainGetCurrentTextureView(swapChain);
  // Getting the texture may fail, in particular if the window has been resized
  // and thus the target surface changed.
  if (!nextTexture) {
    std::cerr << "Cannot acquire next swap chain texture" << std::endl;
    return;
  }

  WGPUCommandEncoderDescriptor commandEncoderDesc = {};
  commandEncoderDesc.nextInChain = nullptr;
  commandEncoderDesc.label = "Command Encoder";
  WGPUCommandEncoder encoder =
      wgpuDeviceCreateCommandEncoder(device, &commandEncoderDesc);

  // Describe a render pass, which targets the texture view
  WGPURenderPassDescriptor renderPassDesc = {};

  WGPURenderPassColorAttachment renderPassColorAttachment = {};
  // The attachment is tighed to the view returned by the swap chain, so that
  // the render pass draws directly on screen.
  renderPassColorAttachment.view = nextTexture;
  // Not relevant here because we do not use multi-sampling
  renderPassColorAttachment.resolveTarget = nullptr;
  renderPassColorAttachment.loadOp = WGPULoadOp_Clear;
  renderPassColorAttachment.storeOp = WGPUStoreOp_Store;
  renderPassColorAttachment.clearValue = clearColor;
  renderPassDesc.colorAttachmentCount = 1;
  renderPassDesc.colorAttachments = &renderPassColorAttachment;

  // No depth buffer for now
  renderPassDesc.depthStencilAttachment = nullptr;

  // We do not use timers for now neither
  // renderPassDesc.timestampWriteCount = 0; // NOT IN DAWN
  renderPassDesc.timestampWrites = 0;
  renderPassDesc.timestampWrites = nullptr;

  renderPassDesc.nextInChain = nullptr;

  // Create a render pass. We end it immediately because we use its built-in
  // mechanism for clearing the screen when it begins (see descriptor).
  WGPURenderPassEncoder renderPass =
      wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

  wgpuRenderPassEncoderSetPipeline(renderPass, pipeline);

  wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, vertexBuffer, 0,
                                       vertexData.size() * sizeof(float));
  wgpuRenderPassEncoderDraw(renderPass, vertexCount, 1, 0, 0);

  wgpuRenderPassEncoderEnd(renderPass);

  wgpuTextureViewRelease(nextTexture);

  WGPUCommandBufferDescriptor cmdBufferDescriptor = {};
  cmdBufferDescriptor.nextInChain = nullptr;
  cmdBufferDescriptor.label = "Command buffer";
  WGPUCommandBuffer command =
      wgpuCommandEncoderFinish(encoder, &cmdBufferDescriptor);
  wgpuQueueSubmit(queue, 1, &command);

  // We can tell the swap chain to present the next texture.
  wgpuSwapChainPresent(swapChain);
}

void Game::quit() { isRunning = false; }

void Game::cleanup() {
  wgpuSwapChainRelease(swapChain);
  wgpuDeviceRelease(device);
  wgpuAdapterRelease(adapter);
  wgpuInstanceRelease(instance);

  SDL_DestroyWindow(window);

  SDL_Quit();
}
