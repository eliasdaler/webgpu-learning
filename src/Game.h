#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <webgpu/webgpu_cpp.h>

#include <Graphics/Camera.h>
#include <Graphics/GPUMesh.h>
#include <Graphics/Material.h>
#include <Graphics/MipMapGenerator.h>
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

        int screenWidth = 1280;
        int screenHeight = 960;

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
    void createSkyboxDrawingPipeline();
    void createSpriteDrawingPipeline();
    void createPostFXDrawingPipeline();
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

    void updateCSMFrustums(const Camera& camera) const;

    void generateDrawList();
    void sortDrawList();

    bool isRunning{false};

    Params params;

    SDL_Window* window{nullptr};

    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::RequiredLimits requiredLimits;

    std::unique_ptr<wgpu::Surface> surface;
    std::unique_ptr<wgpu::SwapChain> swapChain;

    wgpu::TextureFormat swapChainFormat;
    wgpu::Queue queue;

    wgpu::Sampler nearestSampler;
    wgpu::Sampler bilinearSampler;
    wgpu::Sampler anisotropicSampler;

    wgpu::BindGroup perFrameBindGroup;

    wgpu::ShaderModule meshVertexShaderModule;
    wgpu::ShaderModule meshFragmentShaderModule;
    wgpu::BindGroupLayout perFrameDataGroupLayout;
    wgpu::BindGroupLayout materialGroupLayout;
    wgpu::BindGroupLayout meshGroupLayout;
    wgpu::RenderPipeline meshPipeline;

    struct PerFrameData {
        glm::mat4 viewProj;
        glm::mat4 invViewProj;
        glm::vec4 cameraPos;
        glm::vec2 pixelSize;
        glm::vec2 padding; // T_T
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
        Texture texture;
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

    Texture whiteTexture;

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

    MipMapGenerator mipMapGenerator;

    Texture skyboxTexture;
    wgpu::RenderPipeline skyboxPipeline;
    wgpu::ShaderModule skyboxShaderModule;
    wgpu::BindGroupLayout skyboxGroupLayout;

    wgpu::BindGroup skyboxBindGroup;

    Texture screenTexture;
    wgpu::TextureView screenTextureView;
    wgpu::TextureFormat screenTextureFormat;

    wgpu::RenderPipeline postFXPipeline;
    wgpu::ShaderModule postFXShaderModule;
    wgpu::BindGroupLayout postFXScreenTextureGroupLayout;
    wgpu::BindGroup postFXBindGroup;

    wgpu::ShaderModule fullscreenTriangleShaderModule;

    static const std::size_t NUM_SHADOW_CASCADES = 4;
    float cascadedShadowMapSize{4096.f};

    struct CSMData {
        glm::vec4 cascadeFarPlaneZs;
        std::array<glm::mat4, 4> lightSpaceTMs;
    };
    wgpu::Buffer csmDataBuffer;

    // TODO: read from scene
    glm::vec3 sunLightDir{-0.5, -0.7, -1};
};
