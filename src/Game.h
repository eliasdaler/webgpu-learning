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

    struct Material {
        wgpu::Texture texture;
        wgpu::BindGroup bindGroup;
    };

public:
    void start(Params params);

private:
    void init();
    void initCamera();
    void initSceneData();
    void createMeshDrawingPipeline();
    void createSpriteDrawingPipeline();
    void createSprite();
    void createYaeModel();
    void createFloorTile();
    void initImGui();

    Material makeMaterial(const std::filesystem::path& diffusePath, const wgpu::Sampler& sampler);

    void loop();
    void update(float dt);
    void updateDevTools(float dt);
    void render();
    void quit();
    void cleanup();
    void shutdownImGui();

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

    wgpu::BindGroup perFrameBindGroup;

    wgpu::ShaderModule meshShaderModule;
    wgpu::BindGroupLayout perFrameDataGroupLayout;
    wgpu::BindGroupLayout materialGroupLayout;
    wgpu::BindGroupLayout meshGroupLayout;
    wgpu::RenderPipeline meshPipeline;

    struct PerFrameData {
        glm::mat4 viewProj;
        glm::vec4 cameraPos;
    };

    struct MeshData {
        glm::mat4 model;
    };

    struct DirectionalLightData {
        glm::vec4 directionAndMisc; // [pos.x, pos.y, pos.z, unused]
        glm::vec4 colorAndIntensity; // [color.r, color.g, color.b, intensity]
    };

    wgpu::Buffer frameDataBuffer;
    wgpu::Buffer directionalLightBuffer;

    wgpu::TextureFormat depthTextureFormat{wgpu::TextureFormat::Depth24Plus};
    wgpu::Texture depthTexture;
    wgpu::TextureView depthTextureView;

    wgpu::Buffer modelVertexBuffer;
    wgpu::Buffer modelIndexBuffer;
    wgpu::Buffer modelDataBuffer;
    wgpu::BindGroup meshBindGroup;
    Model model;
    float modelRotationAngle{0.f};
    Material meshMaterial;

    wgpu::BindGroup spriteBindGroup;

    wgpu::ShaderModule spriteShaderModule;
    wgpu::BindGroupLayout spriteBindGroupLayout;
    wgpu::RenderPipeline spritePipeline;

    wgpu::Buffer spriteVertexBuffer;
    wgpu::Buffer spriteIndexBuffer;
    wgpu::Texture spriteTexture;

    wgpu::Sampler nearestSampler;

    glm::vec3 cameraPos;
    glm::vec3 cameraDirection;
    glm::mat4 cameraView;
    glm::mat4 cameraProj;

    wgpu::Buffer tileVertexBuffer;
    wgpu::Buffer tileIndexBuffer;
    wgpu::BindGroup tileMeshBindGroup;
    wgpu::Buffer tileMeshDataBuffer;
    Model tileModel;
    Material tileMaterial;

    wgpu::Texture tileTexture;
    wgpu::BindGroup tileMaterialBindGroup;
};
