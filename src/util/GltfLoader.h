#pragma once

#include <filesystem>

#include <Graphics/GPUMesh.h>
#include <Graphics/Material.h>
#include <Graphics/Mesh.h>
#include <Math/Transform.h>

struct Model;
struct Scene;

class MaterialCache;
class MeshCache;
class MipMapGenerator;

namespace util
{
struct LoadContext {
    const wgpu::Device& device;
    const wgpu::Queue& queue;
    const wgpu::BindGroupLayout& materialLayout;

    const wgpu::Sampler& nearestSampler;
    const wgpu::Sampler& linearSampler;

    const Texture& whiteTexture;

    MipMapGenerator& mipMapGenerator;
    MaterialCache& materialCache;
    MeshCache& meshCache;

    wgpu::RequiredLimits requiredLimits;
};

class SceneLoader {
public:
    void loadScene(const LoadContext& context, Scene& scene, const std::filesystem::path& path);

private:
    // gltf material id -> material cache id
    std::unordered_map<std::size_t, MaterialId> materialMapping;

    // gltf node id -> JointId
    // for now only one skeleton per scene is supported
    std::unordered_map<int, JointId> gltfNodeIdxToJointId;
};

}
