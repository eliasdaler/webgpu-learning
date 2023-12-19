#pragma once

#include <string>

#include <webgpu/webgpu.h>

struct SDL_Window;

class Game {
public:
  struct Params {
    void validate();

    int screenWidth = 640;
    int screenHeight = 480;

    std::string windowTitle = "Game";
  };

public:
  void start(Params params);

private:
  void init();
  void loop();
  void update(float dt);
  void render();
  void quit();
  void cleanup();

  bool isRunning{false};

  Params params;

  SDL_Window *window{nullptr};

  WGPUInstance instance;
  WGPUAdapter adapter;
  WGPUSwapChain swapChain;
  WGPUTextureFormat swapChainFormat;
  WGPUDevice device;
  WGPUQueue queue;

  WGPUShaderModule shaderModule;
  WGPURenderPipeline pipeline;
};
