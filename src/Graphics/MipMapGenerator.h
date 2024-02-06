#pragma once

#include <unordered_map>

#include <webgpu/webgpu_cpp.h>

class Texture;

class MipMapGenerator {
public:
    void init(const wgpu::Device& device, const wgpu::ShaderModule& fullscreenTriangleShaderModule);

    const wgpu::BindGroupLayout& getTextureGroupLayout() { return textureGroupLayout; }

    void generateMips(const wgpu::Device& device, const wgpu::Queue& queue, const Texture& texture);

private:
    const wgpu::RenderPipeline& createPipelineForFormat(
        const wgpu::Device& device,
        wgpu::TextureFormat format);
    const wgpu::RenderPipeline& getOrCreatePipeline(
        const wgpu::Device& device,
        wgpu::TextureFormat format);

    void generateMip(
        const wgpu::Device& device,
        const wgpu::Queue& queue,
        const wgpu::CommandEncoder& encoder,
        const wgpu::RenderPipeline pipeline,
        const wgpu::TextureView& inputView,
        const wgpu::TextureView& outputView) const;

    wgpu::ShaderModule fullscreenTriangleShaderModule;
    wgpu::ShaderModule shaderModule;
    wgpu::BindGroupLayout textureGroupLayout;
    wgpu::Sampler linearSampler;

    std::unordered_map<wgpu::TextureFormat, wgpu::RenderPipeline> pipelines;
};
