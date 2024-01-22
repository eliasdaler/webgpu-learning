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
    void initModelStuff();
    void initSpriteStuff();
    void initCamera();

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

    std::unique_ptr<wgpu::Surface> surface;
    std::unique_ptr<wgpu::SwapChain> swapChain;

    wgpu::TextureFormat swapChainFormat;
    wgpu::Queue queue;

    wgpu::ShaderModule shaderModule;

    wgpu::BindGroup perFrameBindGroup;
    wgpu::BindGroup materialBindGroup;
    wgpu::BindGroup meshBindGroup;

    wgpu::RenderPipeline pipeline;

    struct PerFrameData {
        glm::mat4 viewProj;
    };
    wgpu::Buffer frameDataBuffer;

    struct MeshData {
        glm::mat4 model;
    };
    wgpu::Buffer meshDataBuffer;

    struct DirectionalLightData {
        glm::vec4 directionAndMisc; // [pos.x, pos.y, pos.z, unused]
        glm::vec4 colorAndIntensity; // [color.r, color.g, color.b, intensity]
    };
    wgpu::Buffer directionalLightBuffer;

    wgpu::TextureFormat depthTextureFormat{wgpu::TextureFormat::Depth24Plus};
    wgpu::Texture depthTexture;
    wgpu::TextureView depthTextureView;

    wgpu::Buffer vertexBuffer;
    wgpu::Buffer indexBuffer;
    wgpu::Texture texture;

    wgpu::ShaderModule spriteShaderModule;
    wgpu::BindGroup spriteBindGroup;
    wgpu::RenderPipeline spritePipeline;

    wgpu::Buffer spriteVertexBuffer;
    wgpu::Buffer spriteIndexBuffer;
    wgpu::Texture spriteTexture;

    wgpu::Sampler nearestSampler;

    Model model;

    glm::vec3 cameraPos;
    glm::vec3 cameraDirection;
    glm::mat4 cameraView;
    glm::mat4 cameraProj;

    float meshRotationAngle{0.f};
};
