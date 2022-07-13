#pragma once

#include "Common.hpp"

namespace Luna {
namespace Vulkan {
class Context : public IntrusivePtrEnabled<Context, std::default_delete<Context>, HandleCounter> {
 public:
	Context(const std::vector<const char*>& instanceExtensions = {},
	        const std::vector<const char*>& deviceExtensions   = {});
	Context(const Context&)            = delete;
	Context& operator=(const Context&) = delete;
	~Context() noexcept;

	const vk::Device& GetDevice() const {
		return _device;
	}
	const ExtensionInfo& GetExtensionInfo() const {
		return _extensions;
	}
	const vk::PhysicalDevice& GetGPU() const {
		return _gpu;
	}
	const GPUInfo& GetGPUInfo() const {
		return _gpuInfo;
	}
	const vk::Instance& GetInstance() const {
		return _instance;
	}
	const QueueInfo& GetQueueInfo() const {
		return _queues;
	}

 private:
	void CreateInstance(const std::vector<const char*>& requiredExtensions);
	void SelectPhysicalDevice(const std::vector<const char*>& requiredDeviceExtensions);
	void CreateDevice(const std::vector<const char*>& requiredExtensions);

	void DumpInstanceInformation() const;
	void DumpDeviceInformation() const;

	vk::DynamicLoader _loader;
	ExtensionInfo _extensions;
	vk::Instance _instance;
#ifdef LUNA_VULKAN_DEBUG
	vk::DebugUtilsMessengerEXT _debugMessenger;
#endif
	GPUInfo _gpuInfo  = {};
	QueueInfo _queues = {};
	vk::PhysicalDevice _gpu;
	vk::Device _device;
};
}  // namespace Vulkan
}  // namespace Luna
