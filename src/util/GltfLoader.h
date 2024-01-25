#pragma once

#include <filesystem>
#include <vector>

#include <Graphics/GPUMesh.h>
#include <Graphics/Material.h>
#include <Graphics/Mesh.h>
#include <Math/Transform.h>

struct Model;

namespace util
{

struct MeshPrimitive {
    GPUMesh mesh;
    std::size_t materialIndex;
};

struct SceneNode {
    std::string name;
    Transform transform;
    std::size_t meshIndex;

    SceneNode* parent{nullptr};
    std::vector<std::unique_ptr<SceneNode>> children;
};

struct SceneMesh {
    std::vector<MeshPrimitive> primitives;
};

struct Scene {
    std::vector<SceneMesh> meshes;
    std::vector<Material> materials;

    std::vector<std::unique_ptr<SceneNode>> nodes;
};

Model loadModel(const std::filesystem::path& path);

struct LoadContext {
    const wgpu::Device& device;
    const wgpu::Queue& queue;
    const wgpu::BindGroupLayout& materialLayout;
    const wgpu::Sampler& defaultSampler;
};

Scene loadScene(const LoadContext& context, const std::filesystem::path& path);
}
