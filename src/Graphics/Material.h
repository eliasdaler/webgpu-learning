#pragma once

#include <webgpu/webgpu_cpp.h>

struct Material {
    wgpu::Texture texture;
    wgpu::BindGroup bindGroup;
};
