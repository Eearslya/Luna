#include "Device.hpp"

#include "Context.hpp"
#include "Utility/Log.hpp"

namespace Luna {
namespace Vulkan {
Device::Device(const Context& context)
		: _extensions(context.GetExtensionInfo()),
			_instance(context.GetInstance()),
			_gpuInfo(context.GetGPUInfo()),
			_queues(context.GetQueueInfo()),
			_gpu(context.GetGPU()),
			_device(context.GetDevice()) {}

Device::~Device() noexcept {}
}  // namespace Vulkan
}  // namespace Luna
