#pragma once

#include <webgpu/webgpu_cpp.h>

struct GPUMesh {
    wgpu::Buffer vertexBuffer;
    wgpu::Buffer indexBuffer;
    std::size_t indexBufferSize{0};
};
