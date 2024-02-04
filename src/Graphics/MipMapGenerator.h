#pragma once

#include <webgpu/webgpu_cpp.h>

class Texture;

class MipMapGenerator {
public:
    void init(const wgpu::Device& device);

    const wgpu::BindGroupLayout& getTextureGroupLayout() { return textureGroupLayout; }

    void generateMips(const wgpu::Device& device, const wgpu::Queue& queue, const Texture& texture);

private:
    wgpu::ShaderModule shaderModule;
    wgpu::RenderPipeline pipeline;
    wgpu::BindGroupLayout textureGroupLayout;
    wgpu::Sampler linearSampler;
};
