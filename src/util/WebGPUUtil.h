#pragma once

#include <filesystem>

#include <webgpu/webgpu_cpp.h>

namespace util
{
void initWebGPU();
wgpu::Adapter requestAdapter(
    const wgpu::Instance& instance,
    wgpu::RequestAdapterOptions const* options);
wgpu::Device requestDevice(const wgpu::Adapter& adapter, wgpu::DeviceDescriptor const* descriptor);

// WebGPU's writeBuffer only allows to have writeBuffer's contentSize to be a
// multiple of 4 bytes. This means that the number of triangles in index buffer
// has to be even when its type is uint16_t (6*2=12 bytes).
// Otherwise, with uneven number of tris, you get (num tris * 3 * 2) = N*4+2 bytes size buffer.
void padBufferToFourBytes(std::vector<std::uint16_t>& indices);

wgpu::Texture loadTexture(
    const wgpu::Device& device,
    const wgpu::Queue& queue,
    const std::filesystem::path& path,
    wgpu::TextureFormat format);
} // namespace util
