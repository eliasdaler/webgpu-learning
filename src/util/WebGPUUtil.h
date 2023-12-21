#pragma once
#include <webgpu/webgpu_cpp.h>

namespace util
{
void initWebGPU();
wgpu::Adapter requestAdapter(
    const wgpu::Instance& instance,
    wgpu::RequestAdapterOptions const* options);
wgpu::Device requestDevice(wgpu::Adapter adapter, wgpu::DeviceDescriptor const* descriptor);
} // namespace util
