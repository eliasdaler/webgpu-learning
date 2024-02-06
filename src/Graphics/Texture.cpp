#include "Texture.h"

wgpu::TextureView Texture::createView() const
{
    return createView(0, mipLevelCount);
}

wgpu::TextureView Texture::createView(int baseMipLevel, int count) const
{
    const auto textureViewDesc = wgpu::TextureViewDescriptor{
        .format = format,
        .dimension = isCubemap ? wgpu::TextureViewDimension::Cube : wgpu::TextureViewDimension::e2D,
        .baseMipLevel = (std::uint32_t)baseMipLevel,
        .mipLevelCount = (std::uint32_t)count,
        .baseArrayLayer = 0u,
        .arrayLayerCount = isCubemap ? 6u : 1u,
        .aspect = wgpu::TextureAspect::All,
    };
    return texture.CreateView(&textureViewDesc);
}

wgpu::TextureView Texture::createViewForCubeLayer(int baseMipLevel, int count, int layer) const
{
    assert(isCubemap);
    const auto textureViewDesc = wgpu::TextureViewDescriptor{
        .format = format,
        .dimension = wgpu::TextureViewDimension::e2D,
        .baseMipLevel = (std::uint32_t)baseMipLevel,
        .mipLevelCount = (std::uint32_t)count,
        .baseArrayLayer = (std::uint32_t)layer,
        .arrayLayerCount = 1u,
        .aspect = wgpu::TextureAspect::All,
    };
    return texture.CreateView(&textureViewDesc);
}
