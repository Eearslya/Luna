#pragma once

#include "Common.hpp"

namespace Luna {
namespace Vulkan {
class Device : public IntrusivePtrEnabled<Device, std::default_delete<Device>, HandleCounter> {
 public:
	Device(const Context& context);
	Device(const Device&)            = delete;
	Device& operator=(const Device&) = delete;
	~Device() noexcept;

 private:
	const ExtensionInfo _extensions;
	const vk::Instance _instance;
	const GPUInfo _gpuInfo;
	const QueueInfo _queues;
	const vk::PhysicalDevice _gpu;
	const vk::Device _device;
};
}  // namespace Vulkan
}  // namespace Luna
