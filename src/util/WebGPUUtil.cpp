#include "util/WebGPUUtil.h"

#include <array>
#include <iostream>

#include <dawn/dawn_proc.h>
#include <dawn/native/DawnNative.h>

#include <cassert>

#include "ImageLoader.h"

#include <Graphics/MipMapGenerator.h>

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

void defaultShaderCompilationCallback(
    WGPUCompilationInfoRequestStatus status,
    WGPUCompilationInfo const* compilationInfo,
    void* userdata)
{
    const auto* label = reinterpret_cast<const char*>(userdata);
    for (std::size_t i = 0; i < compilationInfo->messageCount; ++i) {
        const auto message = compilationInfo->messages[i];
        if (message.type == (WGPUCompilationMessageType)wgpu::CompilationMessageType::Error) {
            std::cout << "Error: ";
        }
        std::cout << "module \"" << label << "\": " << message.message << "(" << message.linePos
                  << ":" << message.linePos << ")" << std::endl;
    }
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

std::uint32_t calculateMipCount(int imageWidth, int imageHeight)
{
    const auto maxSize = std::max(imageWidth, imageHeight);
    return 1 + std::log2(maxSize);
}

Texture loadTexture(
    const TextureLoadContext& ctx,
    const std::filesystem::path& path,
    wgpu::TextureFormat format,
    bool generateMips)
{
    ImageData data = util::loadImage(path);
    assert(data.channels == 4);
    assert(data.pixels != nullptr);
    assert(format == wgpu::TextureFormat::RGBA8UnormSrgb && "other formats are not yet supported");
    return loadTexture(ctx, format, data, generateMips, path.string().c_str());
}

Texture loadTexture(
    const TextureLoadContext& ctx,
    wgpu::TextureFormat format,
    const ImageData& data,
    bool generateMips,
    const char* label)
{
    const auto mipLevelCount = generateMips ? calculateMipCount(data.width, data.height) : 1;
    const auto textureDesc = wgpu::TextureDescriptor{
        .label = label,
        .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst |
                 wgpu::TextureUsage::RenderAttachment,
        .dimension = wgpu::TextureDimension::e2D,
        .size =
            {
                .width = static_cast<std::uint32_t>(data.width),
                .height = static_cast<std::uint32_t>(data.height),
                .depthOrArrayLayers = 1,
            },
        .format = format,
        .mipLevelCount = mipLevelCount,
        .sampleCount = 1,
    };

    auto texture = ctx.device.CreateTexture(&textureDesc);

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
    ctx.queue
        .WriteTexture(&destination, (void*)data.pixels, pixelsSize, &source, &textureDesc.size);

    auto tex = Texture{
        .texture = texture,
        .mipLevelCount = mipLevelCount,
        .size = {data.width, data.height},
        .format = format,
    };
    if (generateMips) {
        ctx.mipMapGenerator.generateMips(ctx.device, ctx.queue, tex);
    }
    return tex;
}

Texture createPixelTexture(
    const TextureLoadContext& ctx,
    wgpu::TextureFormat format,
    const glm::vec4& color,
    const char* label)
{
    std::array<unsigned char, 4> rawPixelData{
        static_cast<unsigned char>(color[0] * 255),
        static_cast<unsigned char>(color[1] * 255),
        static_cast<unsigned char>(color[2] * 255),
        static_cast<unsigned char>(color[3] * 255)};
    ImageData data{};
    data.pixels = rawPixelData.data();
    data.width = 1;
    data.height = 1;
    data.channels = 4;
    return util::loadTexture(ctx, format, data, false, label);
}

} // end of namespace util
