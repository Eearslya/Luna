#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
class Context : NonCopyable {
 public:
	Context(const std::vector<const char*>& instanceExtensions = {},
	        const std::vector<const char*>& deviceExtensions   = {});
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
	const vk::SurfaceKHR& GetSurface() const {
		return _surface;
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
#ifdef LUNA_DEBUG
	vk::DebugUtilsMessengerEXT _debugMessenger;
#endif
	vk::SurfaceKHR _surface;
	GPUInfo _gpuInfo  = {};
	QueueInfo _queues = {};
	vk::PhysicalDevice _gpu;
	vk::Device _device;
};
}  // namespace Vulkan
}  // namespace Luna
