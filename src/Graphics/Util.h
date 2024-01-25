#pragma once

#include <filesystem>

#include <webgpu/webgpu_cpp.h>

class Mesh;
class GPUMesh;
class Material;

namespace util
{
Material makeMaterial(
    const wgpu::Device& device,
    const wgpu::Queue& queue,
    const std::filesystem::path& diffusePath,
    const wgpu::BindGroupLayout& materialGroupLayout,
    const wgpu::Sampler& sampler);
GPUMesh makeGPUMesh(const wgpu::Device& device, const wgpu::Queue& queue, const Mesh& cpuMesh);
}
