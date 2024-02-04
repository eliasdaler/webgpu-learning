#pragma once

#include <limits>

#include <webgpu/webgpu_cpp.h>

#include <Graphics/Material.h>

using MeshId = std::size_t;
static const auto NULL_MESH_ID = std::numeric_limits<std::size_t>::max();

struct GPUMesh {
    wgpu::Buffer vertexBuffer;

    wgpu::Buffer indexBuffer;
    std::size_t indexBufferSize{0};
    MaterialId materialId{NULL_MATERIAL_ID};

    struct AttribProps {
        std::size_t offset;
        std::size_t size;
    };
    std::vector<AttribProps> attribs;
};
