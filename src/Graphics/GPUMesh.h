#pragma once

#include <webgpu/webgpu_cpp.h>

#include <Graphics/Material.h>

struct GPUMesh {
    wgpu::Buffer vertexBuffer;
    wgpu::Buffer indexBuffer;
    std::size_t indexBufferSize{0};
    MaterialId materialId{NULL_MATERIAL_ID};
};
