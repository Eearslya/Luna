#pragma once

#include <Luna/Vulkan/Common.hpp>
#include <vector>

namespace Luna {
namespace Vulkan {
class Context : public IntrusivePtrEnabled<Context> {
 public:
	Context(const std::vector<const char*>& instanceExtensions = {},
	        const std::vector<const char*>& deviceExtensions   = {});
	~Context() noexcept;

 private:
	void CreateInstance(const std::vector<const char*>& requiredExtensions);
	void SelectPhysicalDevice(const std::vector<const char*>& requiredExtensions);
	void CreateDevice(const std::vector<const char*>& requiredExtensions);

#ifdef LUNA_VULKAN_DEBUG
	void DumpInstanceInformation() const;
	void DumpDeviceInformation() const;
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
