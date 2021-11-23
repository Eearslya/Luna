#include <Luna/Core/Log.hpp>
#include <Luna/Devices/Window.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <unordered_map>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace Luna {
namespace Vulkan {
static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                          VkDebugUtilsMessageTypeFlagsEXT type,
                                                          const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                          void* userData) {
	switch (severity) {
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
			Log::Error("Vulkan ERROR: {}", data->pMessage);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
			Log::Warning("Vulkan Warning: {}", data->pMessage);
			break;
		default:
			Log::Debug("Vulkan: {}", data->pMessage);
			break;
	}

	return VK_FALSE;
}

Context::Context(const std::vector<const char*>& instanceExtensions, const std::vector<const char*>& deviceExtensions) {
	if (!_loader.success()) { throw std::runtime_error("Failed to load Vulkan loader!"); }

	VULKAN_HPP_DEFAULT_DISPATCHER.init(_loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));

	DumpInstanceInformation();
	CreateInstance(instanceExtensions);

	SelectPhysicalDevice(deviceExtensions);
	DumpDeviceInformation();

	CreateDevice(deviceExtensions);
}

Context::~Context() noexcept {
	if (_instance) {
		if (_surface) { _instance.destroySurfaceKHR(_surface); }
#ifdef LUNA_DEBUG
		if (_debugMessenger) { _instance.destroyDebugUtilsMessengerEXT(_debugMessenger); }
#endif
		_instance.destroy();
	}
}

void Context::CreateInstance(const std::vector<const char*>& requiredExtensions) {
	struct Extension {
		std::string Name;
		uint32_t Version;
		std::string Layer;  // Layer will be an empty string if the extension is in the base layer.
	};

	const auto availableLayers = vk::enumerateInstanceLayerProperties();
	std::unordered_map<std::string, Extension> availableExtensions;
	std::vector<const char*> enabledExtensions;
	std::vector<const char*> enabledLayers;

	// Find all of our instance extensions. This will look through all of the available layers, and if an extension is
	// available in multiple ways, it will prefer whatever layer has the highest spec version.
	{
		const auto EnumerateExtensions = [&](const vk::LayerProperties* layer) -> void {
			std::vector<vk::ExtensionProperties> extensions;
			if (layer == nullptr) {
				extensions = vk::enumerateInstanceExtensionProperties(nullptr);
			} else {
				extensions = vk::enumerateInstanceExtensionProperties(std::string(layer->layerName));
			}

			for (const auto& extension : extensions) {
				const std::string name = std::string(extension.extensionName);
				Extension ext{name, extension.specVersion, layer ? std::string(layer->layerName) : ""};
				auto it = availableExtensions.find(name);
				if (it == availableExtensions.end() || it->second.Version < ext.Version) { availableExtensions[name] = ext; }
			}
		};
		EnumerateExtensions(nullptr);
		for (const auto& layer : availableLayers) { EnumerateExtensions(&layer); }
	}

	// Enable all of the required extensions and a handful of preferred extensions or layers. If we can't find any of the
	// required extensions, we fail.
	{
		const auto HasLayer = [&availableLayers](const char* layerName) -> bool {
			for (const auto& layer : availableLayers) {
				if (strcmp(layer.layerName, layerName) == 0) { return true; }
			}

			return false;
		};
		const auto TryLayer = [&](const char* layerName) -> bool {
			if (!HasLayer(layerName)) { return false; }
			for (const auto& name : enabledLayers) {
				if (strcmp(name, layerName) == 0) { return true; }
			}
			Log::Debug("Enabling instance layer '{}'.", layerName);
			enabledLayers.push_back(layerName);
			return true;
		};
		const auto HasExtension = [&availableExtensions](const char* extensionName) -> bool {
			const std::string name(extensionName);
			const auto it = availableExtensions.find(name);
			return it != availableExtensions.end();
		};
		const auto TryExtension = [&](const char* extensionName) -> bool {
			for (const auto& name : enabledExtensions) {
				if (strcmp(name, extensionName) == 0) { return true; }
			}
			if (!HasExtension(extensionName)) { return false; }
			const std::string name(extensionName);
			const auto it = availableExtensions.find(name);
			if (it->second.Layer.length() != 0) { TryLayer(it->second.Layer.c_str()); }
			Log::Debug("Enabling instance extension '{}'.", extensionName);
			enabledExtensions.push_back(extensionName);
			return true;
		};

		for (const auto& ext : requiredExtensions) {
			if (!TryExtension(ext)) {
				throw vk::ExtensionNotPresentError("Extension " + std::string(ext) + " was not available!");
			}
		}

		TryExtension("VK_KHR_portability_subset");
		_extensions.GetPhysicalDeviceProperties2 = TryExtension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
		_extensions.GetSurfaceCapabilities2      = TryExtension(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);

#ifdef LUNA_DEBUG
		TryLayer("VK_LAYER_KHRONOS_validation");

		_extensions.DebugUtils         = TryExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		_extensions.ValidationFeatures = TryExtension(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
#endif
	}

	const vk::ApplicationInfo appInfo(
		"Luna", VK_MAKE_API_VERSION(0, 1, 0, 0), "Luna", VK_MAKE_API_VERSION(0, 1, 0, 0), VK_API_VERSION_1_0);
	const vk::InstanceCreateInfo instanceCI({}, &appInfo, enabledLayers, enabledExtensions);

#ifdef LUNA_DEBUG
	const vk::DebugUtilsMessengerCreateInfoEXT debugCI(
		{},
		vk::DebugUtilsMessageSeverityFlagBitsEXT::eError | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning,
		vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
			vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
		VulkanDebugCallback,
		this);
	const std::vector<vk::ValidationFeatureEnableEXT> validationEnable = {vk::ValidationFeatureEnableEXT::eBestPractices};
	const std::vector<vk::ValidationFeatureDisableEXT> validationDisable;
	const vk::ValidationFeaturesEXT validationCI(validationEnable, validationDisable);

	vk::StructureChain<vk::InstanceCreateInfo, vk::DebugUtilsMessengerCreateInfoEXT, vk::ValidationFeaturesEXT> chain(
		instanceCI, debugCI, validationCI);

	if (!_extensions.DebugUtils) { chain.unlink<vk::DebugUtilsMessengerCreateInfoEXT>(); }
	if (!_extensions.ValidationFeatures) { chain.unlink<vk::ValidationFeaturesEXT>(); }

	_instance = vk::createInstance(chain.get());
#else
	_instance = vk::createInstance(instanceCI);
#endif

	VULKAN_HPP_DEFAULT_DISPATCHER.init(_instance);

#ifdef LUNA_DEBUG
	if (_extensions.DebugUtils) { _debugMessenger = _instance.createDebugUtilsMessengerEXT(debugCI); }
#endif

	_surface = Window::Get()->CreateSurface(_instance);
}

void Context::SelectPhysicalDevice(const std::vector<const char*>& requiredDeviceExtensions) {
	const auto gpus = _instance.enumeratePhysicalDevices();
	for (const auto& gpu : gpus) {
		GPUInfo gpuInfo;

		// Enumerate basic information.
		gpuInfo.AvailableExtensions = gpu.enumerateDeviceExtensionProperties(nullptr);
		gpuInfo.Layers              = gpu.enumerateDeviceLayerProperties();
		gpuInfo.Memory              = gpu.getMemoryProperties();
		gpuInfo.QueueFamilies       = gpu.getQueueFamilyProperties();

		// Find any extensions hidden within enabled layers.
		for (const auto& layer : gpuInfo.Layers) {
			const auto layerExtensions = gpu.enumerateDeviceExtensionProperties(std::string(layer.layerName));
			for (const auto& ext : layerExtensions) {
				auto it = std::find_if(gpuInfo.AvailableExtensions.begin(),
				                       gpuInfo.AvailableExtensions.end(),
				                       [ext](const vk::ExtensionProperties& props) -> bool {
																 return strcmp(props.extensionName, ext.extensionName) == 0;
															 });
				if (it == gpuInfo.AvailableExtensions.end()) {
					gpuInfo.AvailableExtensions.push_back(ext);
				} else if (it->specVersion < ext.specVersion) {
					it->specVersion = ext.specVersion;
				}
			}
		}

		const auto HasExtension = [&](const char* extensionName) -> bool {
			return std::find_if(gpuInfo.AvailableExtensions.begin(),
			                    gpuInfo.AvailableExtensions.end(),
			                    [extensionName](const vk::ExtensionProperties& props) -> bool {
														return strcmp(extensionName, props.extensionName) == 0;
													}) != gpuInfo.AvailableExtensions.end();
		};

		// Enumerate all of the properties and features.
		if (_extensions.GetPhysicalDeviceProperties2) {
			vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceTimelineSemaphoreFeatures> features;
			vk::StructureChain<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceTimelineSemaphoreProperties> properties;

			if (!HasExtension(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME)) {
				features.unlink<vk::PhysicalDeviceTimelineSemaphoreFeatures>();
				properties.unlink<vk::PhysicalDeviceTimelineSemaphoreProperties>();
			}

			gpu.getFeatures2(&features.get());
			gpu.getProperties2(&properties.get());

			gpuInfo.AvailableFeatures.Features          = features.get().features;
			gpuInfo.AvailableFeatures.TimelineSemaphore = features.get<vk::PhysicalDeviceTimelineSemaphoreFeatures>();

			gpuInfo.Properties.Properties        = properties.get().properties;
			gpuInfo.Properties.TimelineSemaphore = properties.get<vk::PhysicalDeviceTimelineSemaphoreProperties>();
		} else {
			gpuInfo.AvailableFeatures.Features = gpu.getFeatures();
			gpuInfo.Properties.Properties      = gpu.getProperties();
		}

		// Validate that the device meets requirements.
		bool extensions = true;
		for (const auto& ext : requiredDeviceExtensions) {
			if (!HasExtension(ext)) {
				extensions = false;
				break;
			}
		}
		if (!extensions) { continue; }

		bool graphicsQueue = false;
		for (size_t q = 0; q < gpuInfo.QueueFamilies.size(); ++q) {
			const auto& family = gpuInfo.QueueFamilies[q];
			const auto present = gpu.getSurfaceSupportKHR(q, _surface);
			if (family.queueFlags & vk::QueueFlagBits::eGraphics && family.queueFlags & vk::QueueFlagBits::eCompute &&
			    present) {
				graphicsQueue = true;
				break;
			}
		}
		if (!graphicsQueue) { continue; }

		// We have a winner!
		_gpu     = gpu;
		_gpuInfo = gpuInfo;
		return;
	}

	throw std::runtime_error("Failed to find a compatible physical device!");
}

void Context::CreateDevice(const std::vector<const char*>& requiredExtensions) {}

void Context::DumpInstanceInformation() const {
	Log::Trace("----- Vulkan Global Information -----");

	const auto instanceVersion = vk::enumerateInstanceVersion();
	Log::Trace("Instance Version: {}.{}.{}.{}",
	           VK_API_VERSION_VARIANT(instanceVersion),
	           VK_API_VERSION_MAJOR(instanceVersion),
	           VK_API_VERSION_MINOR(instanceVersion),
	           VK_API_VERSION_PATCH(instanceVersion));

	const auto instanceExtensions = vk::enumerateInstanceExtensionProperties(nullptr);
	Log::Trace("Instance Extensions ({}):", instanceExtensions.size());
	for (const auto& ext : instanceExtensions) { Log::Trace(" - {} v{}", ext.extensionName, ext.specVersion); }

	const auto instanceLayers = vk::enumerateInstanceLayerProperties();
	Log::Trace("Instance Layers ({}):", instanceLayers.size());
	for (const auto& layer : instanceLayers) {
		Log::Trace(" - {} v{} (Vulkan {}.{}.{}) - {}",
		           layer.layerName,
		           layer.implementationVersion,
		           VK_API_VERSION_MAJOR(layer.specVersion),
		           VK_API_VERSION_MINOR(layer.specVersion),
		           VK_API_VERSION_PATCH(layer.specVersion),
		           layer.description);
		const auto layerExtensions = vk::enumerateInstanceExtensionProperties(std::string(layer.layerName));
		for (const auto& ext : layerExtensions) { Log::Trace("  - {} v{}", ext.extensionName, ext.specVersion); }
	}

	Log::Trace("----- End Vulkan Global Information -----");
}

void Context::DumpDeviceInformation() const {
	Log::Trace("----- Vulkan Physical Device Info -----");

	Log::Trace("- Device Name: {}", _gpuInfo.Properties.Properties.deviceName);
	Log::Trace("- Device Type: {}", vk::to_string(_gpuInfo.Properties.Properties.deviceType));
	Log::Trace("- Device Driver Version: {}.{}.{}",
	           VK_API_VERSION_MAJOR(_gpuInfo.Properties.Properties.driverVersion),
	           VK_API_VERSION_MINOR(_gpuInfo.Properties.Properties.driverVersion),
	           VK_API_VERSION_PATCH(_gpuInfo.Properties.Properties.driverVersion));

	Log::Trace("- Layers ({}):", _gpuInfo.Layers.size());
	for (const auto& layer : _gpuInfo.Layers) {
		Log::Trace(" - {} v{} (Vulkan {}.{}.{}) - {}",
		           layer.layerName,
		           layer.implementationVersion,
		           VK_API_VERSION_MAJOR(layer.specVersion),
		           VK_API_VERSION_MINOR(layer.specVersion),
		           VK_API_VERSION_PATCH(layer.specVersion),
		           layer.description);
	}

	Log::Trace("- Device Extensions ({}):", _gpuInfo.AvailableExtensions.size());
	for (const auto& ext : _gpuInfo.AvailableExtensions) { Log::Trace("  - {} v{}", ext.extensionName, ext.specVersion); }

	Log::Trace("- Memory Heaps ({}):", _gpuInfo.Memory.memoryHeapCount);
	for (size_t i = 0; i < _gpuInfo.Memory.memoryHeapCount; ++i) {
		const auto& heap = _gpuInfo.Memory.memoryHeaps[i];
		Log::Trace("  - {} {}", FormatSize(heap.size), vk::to_string(heap.flags));
	}
	Log::Trace("- Memory Types ({}):", _gpuInfo.Memory.memoryTypeCount);
	for (size_t i = 0; i < _gpuInfo.Memory.memoryTypeCount; ++i) {
		const auto& type = _gpuInfo.Memory.memoryTypes[i];
		Log::Trace("  - Heap {} {}", type.heapIndex, vk::to_string(type.propertyFlags));
	}

	Log::Trace("- Queue Families ({}):", _gpuInfo.QueueFamilies.size());
	for (size_t i = 0; i < _gpuInfo.QueueFamilies.size(); ++i) {
		const auto& family = _gpuInfo.QueueFamilies[i];
		Log::Trace("  - Family {}: {} Queues {} Granularity {}x{}x{} TimestampBits {}",
		           i,
		           family.queueCount,
		           vk::to_string(family.queueFlags),
		           family.minImageTransferGranularity.width,
		           family.minImageTransferGranularity.height,
		           family.minImageTransferGranularity.depth,
		           family.timestampValidBits);
	}

	Log::Trace("----- End Vulkan Physical Device Info -----");
}
}  // namespace Vulkan
}  // namespace Luna
