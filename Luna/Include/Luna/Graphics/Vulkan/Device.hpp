#pragma once

#include <Luna/Graphics/Vulkan/Common.hpp>
#include <array>
#include <set>
#include <vulkan/vulkan.hpp>

#ifdef LUNA_DEBUG
#	define LUNA_VULKAN_DEBUG
#endif

namespace Luna {
namespace Graphics {
namespace Vulkan {
class Device final {
 public:
	Device();
	~Device() noexcept;

 private:
	void DumpInstanceInformation() const;
	void CreateInstance();
	void SelectPhysicalDevice();
	void CreateDevice();
	void DumpDeviceInformation() const;

	vk::DynamicLoader _loader;
	ExtensionInfo _extensions;
	vk::Instance _instance;
#ifdef LUNA_VULKAN_DEBUG
	vk::DebugUtilsMessengerEXT _debugMessenger;
#endif
	vk::SurfaceKHR _surface;
	GPUInfo _gpuInfo  = {};
	QueueInfo _queues = {};
	vk::PhysicalDevice _gpu;
	vk::Device _device;
};
}  // namespace Vulkan
}  // namespace Graphics
}  // namespace Luna
