#pragma once

#include <string>

#include <webgpu/webgpu_cpp.h>

#include <glm/vec4.hpp>

struct MaterialData {
    glm::vec4 baseColor;
};

struct Material {
    std::string name;

    wgpu::Buffer dataBuffer;
    wgpu::Texture diffuseTexture;
    glm::vec4 baseColor{1.f, 1.f, 1.f, 1.f};

    wgpu::BindGroup bindGroup;
};
