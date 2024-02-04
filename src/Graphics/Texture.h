#pragma once

#include <glm/vec2.hpp>

#include <webgpu/webgpu_cpp.h>

struct Texture {
    wgpu::Texture texture;
    std::uint32_t mipLevelCount{0};
    glm::ivec2 size{};
    wgpu::TextureFormat format;

    wgpu::TextureView createView() const;
    wgpu::TextureView createView(int baseMipLevel, int count) const;
};
