#include "Util.h"

#include <Graphics/GPUMesh.h>
#include <Graphics/Material.h>
#include <Graphics/Mesh.h>

#include <util/WebGPUUtil.h>

namespace util
{
GPUMesh makeGPUMesh(const wgpu::Device& device, const wgpu::Queue& queue, const Mesh& cpuMesh)
{
    GPUMesh mesh;

    { // vertex buffer
        const auto bufferDesc = wgpu::BufferDescriptor{
            .usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst,
            .size = cpuMesh.vertices.size() * sizeof(Mesh::Vertex),
        };

        mesh.vertexBuffer = device.CreateBuffer(&bufferDesc);
        queue.WriteBuffer(mesh.vertexBuffer, 0, cpuMesh.vertices.data(), bufferDesc.size);
    }

    { // index buffer
        const auto bufferDesc = wgpu::BufferDescriptor{
            .usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst,
            .size = cpuMesh.indices.size() * sizeof(std::uint16_t),
        };

        mesh.indexBuffer = device.CreateBuffer(&bufferDesc);
        queue.WriteBuffer(mesh.indexBuffer, 0, cpuMesh.indices.data(), bufferDesc.size);
    }

    return mesh;
}

Material makeMaterial(
    const wgpu::Device& device,
    const wgpu::Queue& queue,
    const std::filesystem::path& diffusePath,
    const wgpu::BindGroupLayout& materialGroupLayout,
    const wgpu::Sampler& sampler)
{
    Material material;

    material.diffuseTexture =
        util::loadTexture(device, queue, diffusePath, wgpu::TextureFormat::RGBA8UnormSrgb);

    { // material data
        const auto textureViewDesc = wgpu::TextureViewDescriptor{
            .format = wgpu::TextureFormat::RGBA8UnormSrgb,
            .dimension = wgpu::TextureViewDimension::e2D,
            .baseMipLevel = 0,
            .mipLevelCount = 1,
            .baseArrayLayer = 0,
            .arrayLayerCount = 1,
            .aspect = wgpu::TextureAspect::All,
        };
        const auto textureView = material.diffuseTexture.CreateView(&textureViewDesc);

        const std::array<wgpu::BindGroupEntry, 2> bindings{{
            {
                .binding = 0,
                .textureView = textureView,
            },
            {
                .binding = 1,
                .sampler = sampler,
            },
        }};
        const auto bindGroupDesc = wgpu::BindGroupDescriptor{
            .layout = materialGroupLayout.Get(),
            .entryCount = bindings.size(),
            .entries = bindings.data(),
        };

        material.bindGroup = device.CreateBindGroup(&bindGroupDesc);
    }

    return material;
}

}
