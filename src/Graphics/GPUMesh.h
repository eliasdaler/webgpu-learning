#pragma once

#include <limits>

#include <glm/vec3.hpp>

#include <webgpu/webgpu_cpp.h>

#include <Graphics/Material.h>
#include <Math/Sphere.h>

using MeshId = std::size_t;
static const auto NULL_MESH_ID = std::numeric_limits<std::size_t>::max();

struct GPUMesh {
    wgpu::Buffer indexBuffer;
    std::uint32_t indexBufferSize{0};
    wgpu::Buffer vertexBuffer;

    MaterialId materialId{NULL_MATERIAL_ID};

    struct AttribProps {
        std::uint64_t offset;
        std::uint64_t size;
    };
    std::vector<AttribProps> attribs;

    // AABB
    glm::vec3 minPos;
    glm::vec3 maxPos;
    math::Sphere boundingSphere;

    bool hasSkeleton{false};
};
