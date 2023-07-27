#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
class Context : public VulkanObject<Context> {
	friend class Device;

 public:
	Context(const std::vector<const char*>& instanceExtensions = {},
	        const std::vector<const char*>& deviceExtensions   = {});
	~Context() noexcept;

 private:
	static constexpr uint32_t TargetVulkanVersion = VK_API_VERSION_1_2;

	void CreateInstance(const std::vector<const char*>& requiredExtensions);
	void SelectPhysicalDevice(const std::vector<const char*>& requiredExtensions);
	void CreateDevice(const std::vector<const char*>& requiredExtensions);

#ifdef LUNA_VULKAN_DEBUG
	void DumpInstanceInfo() const;
	void DumpDeviceInfo() const;
#endif

	vk::DynamicLoader _loader;
	Extensions _extensions;
	vk::Instance _instance;
#ifdef LUNA_VULKAN_DEBUG
	vk::DebugUtilsMessengerEXT _debugMessenger;
#endif
	DeviceInfo _deviceInfo;
	QueueInfo _queueInfo;
	vk::Device _device;
};
}  // namespace Vulkan
}  // namespace Luna
