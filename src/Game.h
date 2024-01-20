#pragma once

#include <memory>
#include <string>

#include <webgpu/webgpu_cpp.h>

#include <Graphics/Model.h>

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

    SDL_Window* window{nullptr};

    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;

    std::unique_ptr<wgpu::SwapChain> swapChain;
    wgpu::TextureFormat swapChainFormat;
    wgpu::Queue queue;

    wgpu::ShaderModule shaderModule;
    wgpu::RenderPipeline pipeline;

    wgpu::Buffer vertexBuffer;
    wgpu::Buffer indexBuffer;
    wgpu::Surface surface;

    wgpu::Texture texture;

    wgpu::BindGroupLayout bindGroupLayout;
    wgpu::BindGroup bindGroup;

    Model model;
};
