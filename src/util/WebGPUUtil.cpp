#include "util/WebGPUUtil.h"

#include <iostream>

#include <dawn/dawn_proc.h>
#include <dawn/native/DawnNative.h>

#include <cassert>

#include "ImageLoader.h"

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

void padBufferToFourBytes(std::vector<std::uint16_t>& indices)
{
    if (indices.size() % 4 == 0) {
        return;
    }

    if (indices.size() == 2) { // TEMP HACK for broken meshes
        indices.push_back(0);
    }

    assert(indices.size() % 3 == 0 && "Number of indices not divisible by 3");
    if (indices.size() % 6 != 0) {
        for (int i = 0; i < 3; ++i) {
            indices.push_back(0);
        }
    }
}

wgpu::Texture loadTexture(
    const wgpu::Device& device,
    const wgpu::Queue& queue,
    const std::filesystem::path& path,
    wgpu::TextureFormat format)
{
    ImageData data = util::loadImage(path);
    assert(data.channels == 4);
    assert(data.pixels != nullptr);
    assert(format == wgpu::TextureFormat::RGBA8UnormSrgb && "other formats are not yet supported");
    return loadTexture(device, queue, format, data, path.string().c_str());

    const auto textureDesc = wgpu::TextureDescriptor{
        .label = path.string().c_str(), // sadly these labels don't show up in RenderDoc
        .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
        .dimension = wgpu::TextureDimension::e2D,
        .size =
            {
                .width = static_cast<std::uint32_t>(data.width),
                .height = static_cast<std::uint32_t>(data.height),
                .depthOrArrayLayers = 1,
            },
        .format = format,
        .mipLevelCount = 1,
        .sampleCount = 1,
    };

    auto texture = device.CreateTexture(&textureDesc);

    // copy data to GPU
    const wgpu::ImageCopyTexture destination{
        .texture = texture,
        .mipLevel = 0,
        .origin = {0, 0, 0},
    };
    const wgpu::TextureDataLayout source{
        .bytesPerRow = static_cast<std::uint32_t>(data.width * data.channels),
        .rowsPerImage = static_cast<std::uint32_t>(data.height)};

    const auto pixelsSize = data.width * data.height * data.channels;
    queue.WriteTexture(&destination, (void*)data.pixels, pixelsSize, &source, &textureDesc.size);

    return texture;
}

wgpu::Texture loadTexture(
    const wgpu::Device& device,
    const wgpu::Queue& queue,
    wgpu::TextureFormat format,
    const ImageData& data,
    const char* label)
{
    const auto textureDesc = wgpu::TextureDescriptor{
        .label = label, // sadly these labels don't show up in RenderDoc
        .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
        .dimension = wgpu::TextureDimension::e2D,
        .size =
            {
                .width = static_cast<std::uint32_t>(data.width),
                .height = static_cast<std::uint32_t>(data.height),
                .depthOrArrayLayers = 1,
            },
        .format = format,
        .mipLevelCount = 1,
        .sampleCount = 1,
    };

    auto texture = device.CreateTexture(&textureDesc);

    // copy data to GPU
    const wgpu::ImageCopyTexture destination{
        .texture = texture,
        .mipLevel = 0,
        .origin = {0, 0, 0},
    };
    const wgpu::TextureDataLayout source{
        .bytesPerRow = static_cast<std::uint32_t>(data.width * data.channels),
        .rowsPerImage = static_cast<std::uint32_t>(data.height)};

    const auto pixelsSize = data.width * data.height * data.channels;
    queue.WriteTexture(&destination, (void*)data.pixels, pixelsSize, &source, &textureDesc.size);

    return texture;
}

} // end of namespace util
