#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
/**
 * The core class of the Vulkan backend. This class is responsible for creating the Vulkan Instance, selecting a
 * Physical Device, and creating a Logical Device.
 */
class Context : public IntrusivePtrEnabled<Context> {
	friend class Device;

 public:
	/**
	 * Initialize a new Vulkan Context. This function will create an Instance, search for a suitable Physical Device, and
	 * create a Logical Device.
	 *
	 * @param instanceExtensions A list of required Instance extensions. Missing extensions will cause initialization to
	 * fail.
	 * @param deviceExtensions A list of required Device extensions. Missing extensions will cause initialization to fail.
	 */
	Context(const std::vector<const char*>& instanceExtensions = {},
	        const std::vector<const char*>& deviceExtensions   = {});
	Context(const Context&)        = delete;
	void operator=(const Context&) = delete;
	~Context() noexcept;

	const Extensions& GetExtensions() const {
		return _extensions;
	}
	vk::Instance GetInstance() const {
		return _instance;
	}
	const DeviceInfo& GetDeviceInfo() const {
		return _deviceInfo;
	}
	const QueueInfo& GetQueueInfo() const {
		return _queueInfo;
	}
	vk::Device GetDevice() const {
		return _device;
	}

 private:
	/** Our target Vulkan API version. If the device cannot support at least this version, we will fail to initialize. */
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
