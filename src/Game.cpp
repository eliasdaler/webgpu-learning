#include "Game.h"

#include "util/OSUtil.h"
#include "util/SDLWebGPU.h"
#include "util/WebGPUUtil.h"

#include <SDL.h>

#include <webgpu/webgpu.h>

#include <cassert>
#include <filesystem>
#include <iostream>

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
  /* WGPUTextureFormat swapChainFormat =
      wgpuSurfaceGetPreferredFormat(surface, adapter);
  swapChainDesc.format = swapChainFormat; */
  swapChainDesc.format = WGPUTextureFormat_BGRA8Unorm;
  swapChainDesc.usage = WGPUTextureUsage_RenderAttachment;
  swapChainDesc.presentMode = WGPUPresentMode_Fifo;

  swapChain = wgpuDeviceCreateSwapChain(device, surface, &swapChainDesc);
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
