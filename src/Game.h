#pragma once

#include <memory>
#include <string>

#include <webgpu/webgpu_cpp.h>

#include <Graphics/Camera.h>
#include <Graphics/GPUMesh.h>
#include <Graphics/Material.h>
#include <Graphics/Model.h>
#include <Graphics/Scene.h>

#include "FreeCameraController.h"

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
    void initCamera();
    void initSceneData();
    void createMeshDrawingPipeline();
    void createSpriteDrawingPipeline();
    void createSprite();
    void createYaeModel();
    void createFloorTile();
    void initImGui();

    void loop();
    void update(float dt);
    void handleInput(float dt);
    void updateDevTools(float dt);
    void render();
    void quit();
    void cleanup();
    void shutdownImGui();

    void generateDrawList();

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

    wgpu::Sampler nearestSampler;

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

    wgpu::Buffer modelDataBuffer;
    wgpu::BindGroup meshBindGroup;
    glm::vec3 yaePos;
    float modelRotationAngle{0.f};
    Material meshMaterial;
    GPUMesh yaeMesh;

    wgpu::ShaderModule spriteShaderModule;
    wgpu::BindGroupLayout spriteBindGroupLayout;
    wgpu::RenderPipeline spritePipeline;

    wgpu::BindGroup spriteBindGroup;
    wgpu::Buffer spriteVertexBuffer;
    wgpu::Buffer spriteIndexBuffer;
    wgpu::Texture spriteTexture;

    wgpu::BindGroup tileMeshBindGroup;
    wgpu::Buffer tileMeshDataBuffer;
    Material tileMaterial;
    GPUMesh tileMesh;

    Camera camera;
    FreeCameraController cameraController;

    Scene scene;

    struct DrawCommand {
        GPUMesh mesh;
        wgpu::BindGroup meshBindGroup;
        std::size_t materialIndex;
    };

    std::vector<DrawCommand> drawCommands;

    static const std::size_t NULL_ENTITY_ID = std::numeric_limits<std::size_t>::max();
    static const std::size_t NULL_MESH_ID = std::numeric_limits<std::size_t>::max();

    struct Entity {
        std::size_t id{NULL_ENTITY_ID};
        std::string name;

        // transform
        Transform transform;

        // mesh
        std::size_t meshIdx{NULL_ENTITY_ID};
        wgpu::Buffer meshDataBuffer;
        wgpu::BindGroup meshBindGroup;
    };

    std::vector<std::unique_ptr<Entity>> entities;
    Entity& makeNewEntity();

    void createEntitiesFromScene(const Scene& scene);
    void createEntitiesFromNode(const SceneNode& node);
};
