#pragma once

#include <filesystem>

#include <Graphics/GPUMesh.h>
#include <Graphics/Material.h>
#include <Graphics/Mesh.h>
#include <Math/Transform.h>

struct Model;
struct Scene;

namespace util
{

Model loadModel(const std::filesystem::path& path);

struct LoadContext {
    const wgpu::Device& device;
    const wgpu::Queue& queue;
    const wgpu::BindGroupLayout& materialLayout;
    const wgpu::Sampler& defaultSampler;
};

void loadScene(const LoadContext& context, Scene& scene, const std::filesystem::path& path);
}
