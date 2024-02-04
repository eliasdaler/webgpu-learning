#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <webgpu/webgpu_cpp.h>

#include <Graphics/Camera.h>
#include <Graphics/GPUMesh.h>
#include <Graphics/Material.h>
#include <Graphics/Scene.h>
#include <Graphics/SkeletonAnimator.h>

#include "FreeCameraController.h"
#include "MaterialCache.h"
#include "MeshCache.h"

struct SDL_Window;

class Game {
public:
    struct Params {
        void validate();

        int screenWidth = 640;
        int screenHeight = 480;

        std::string windowTitle = "Game";
    };

    static const std::size_t NULL_ENTITY_ID = std::numeric_limits<std::size_t>::max();

    using EntityId = std::size_t;

    struct Entity {
        EntityId id{NULL_ENTITY_ID};
        std::string tag;

        // transform
        Transform transform; // local (relative to parent)
        glm::mat4 worldTransform{1.f};

        // hierarchy
        EntityId parentId{NULL_ENTITY_ID};
        std::vector<EntityId> children;

        // mesh (only one mesh per entity supported for now)
        std::vector<MeshId> meshes;
        std::vector<wgpu::BindGroup> meshBindGroups;
        wgpu::Buffer meshDataBuffer; // where model matrix is stored

        // skeleton
        Skeleton skeleton;
        wgpu::Buffer jointMatricesDataBuffer;
        bool hasSkeleton{false};

        // animation
        SkeletonAnimator skeletonAnimator;
        std::unordered_map<std::string, SkeletalAnimation> animations;

        void uploadJointMatricesToGPU(
            const wgpu::Queue& queue,
            const std::vector<glm::mat4>& jointMatrices) const;
    };

    struct DrawCommand {
        const GPUMesh& mesh;
        wgpu::BindGroup meshBindGroup;
        std::size_t meshId;
    };

public:
    void start(Params params);

private:
    void init();
    void initSwapChain(bool vSync);
    void initCamera();
    void initSceneData();
    void createMeshDrawingPipeline();
    void createSpriteDrawingPipeline();
    void initImGui();

    void loop();
    void update(float dt);
    void handleInput(float dt);
    void updateDevTools(float dt);
    void render();
    void quit();
    void cleanup();
    void shutdownImGui();

    void updateEntityTransforms();
    void updateEntityTransforms(Entity& e, const glm::mat4& parentWorldTransform);

    void generateDrawList();
    void sortDrawList();

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

    wgpu::ShaderModule spriteShaderModule;
    wgpu::BindGroupLayout spriteBindGroupLayout;
    wgpu::RenderPipeline spritePipeline;

    struct Sprite {
        wgpu::Buffer vertexBuffer;
        wgpu::Buffer indexBuffer;
        wgpu::Texture texture;
        wgpu::BindGroup bindGroup;
    };
    void createSprite(Sprite& sprite, const std::filesystem::path& texturePath);

    Sprite sprite;

    Camera camera;
    FreeCameraController cameraController;

    std::vector<std::unique_ptr<Entity>> entities;
    Entity& makeNewEntity();
    Entity& findEntityByName(std::string_view name) const;

    Scene loadScene(const std::filesystem::path& path);
    void createEntitiesFromScene(const Scene& scene);
    EntityId createEntitiesFromNode(
        const Scene& scene,
        const SceneNode& node,
        EntityId parentId = NULL_ENTITY_ID);

    std::vector<DrawCommand> drawCommands;
    std::vector<std::size_t> sortedDrawCommands;

    wgpu::Texture whiteTexture;

    bool vSync{true};
    bool frameLimit{true};
    float frameTime{0.f};
    float avgFPS{0.f};

    // only display update FPS every 1 seconds, otherwise it's too noisy
    float displayedFPS{0.f};
    float displayFPSDelay{1.f};

    MaterialCache materialCache;
    MeshCache meshCache;

    wgpu::Buffer emptyStorageBuffer;
};
