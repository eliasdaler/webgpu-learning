#include "Texture.h"

wgpu::TextureView Texture::createView() const
{
    return createView(0, mipLevelCount);
}

wgpu::TextureView Texture::createView(int baseMipLevel, int count) const
{
    const auto textureViewDesc = wgpu::TextureViewDescriptor{
        .format = format,
        .dimension = wgpu::TextureViewDimension::e2D,
        .baseMipLevel = (std::uint32_t)baseMipLevel,
        .mipLevelCount = (std::uint32_t)count,
        .aspect = wgpu::TextureAspect::All,
    };
    return texture.CreateView(&textureViewDesc);
}
