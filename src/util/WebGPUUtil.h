#include <webgpu/webgpu.h>

namespace util {
void initWebGPU();
WGPUAdapter requestAdapter(WGPUInstance instance,
                           WGPURequestAdapterOptions const *options);
WGPUDevice requestDevice(WGPUAdapter adapter,
                         WGPUDeviceDescriptor const *descriptor);
} // namespace util
