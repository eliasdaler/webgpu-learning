#include "util/WebGPUUtil.h"

#include <iostream>

#include <dawn/dawn_proc.h>
#include <dawn/native/DawnNative.h>

#include <cassert>

namespace util
{

void initWebGPU()
{
    dawnProcSetProcs(&dawn::native::GetProcs());
}

wgpu::Adapter requestAdapter(
    const wgpu::Instance& instance,
    wgpu::RequestAdapterOptions const* options)
{
    auto onAdapterRequestEnded = [](WGPURequestAdapterStatus status,
                                    WGPUAdapter adapter,
                                    char const* message,
                                    void* pUserData) {
        if (status == WGPURequestAdapterStatus_Success) {
            // std::cout << "Got WebGPU adapter" << std::endl;
            *static_cast<wgpu::Adapter*>(pUserData) = wgpu::Adapter::Acquire(adapter);
        } else {
            std::cout << "Could not get WebGPU adapter: " << message << std::endl;
        }
    };

    wgpu::Adapter adapter;
    instance.RequestAdapter(options, onAdapterRequestEnded, (void*)&adapter);
    return adapter;
}

wgpu::Device requestDevice(const wgpu::Adapter& adapter, wgpu::DeviceDescriptor const* descriptor)
{
    auto onDeviceRequestEnded = [](WGPURequestDeviceStatus status,
                                   WGPUDevice device,
                                   char const* message,
                                   void* pUserData) {
        if (status == WGPURequestDeviceStatus_Success) {
            *static_cast<wgpu::Device*>(pUserData) = wgpu::Device::Acquire(device);
        } else {
            std::cout << "Could not get WebGPU device: " << message << std::endl;
        }
    };

    wgpu::Device device;
    adapter.RequestDevice(descriptor, onDeviceRequestEnded, (void*)&device);
    return device;
}

void insertFakeTriangleIfNeeded(std::vector<std::uint16_t>& indices)
{
    assert(indices.size() % 3 == 0 && "Number of indices not divisible by 3");
    if (indices.size() % 6 != 0) {
        for (int i = 0; i < 3; ++i) {
            indices.push_back(0);
        }
    }
}

} // end of namespace util
