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
	if (_device) {
		_device.waitIdle();
		_device.destroy();
	}
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

void Context::CreateDevice(const std::vector<const char*>& requiredExtensions) {
	std::vector<const char*> enabledExtensions;
	// Find and enable all required extensions, and any extensions we would like to have but are not required.
	{
		const auto HasExtension = [&](const char* extensionName) -> bool {
			for (const auto& ext : _gpuInfo.AvailableExtensions) {
				if (strcmp(ext.extensionName, extensionName) == 0) { return true; }
			}
			return false;
		};
		const auto TryExtension = [&](const char* extensionName) -> bool {
			if (!HasExtension(extensionName)) { return false; }
			for (const auto& name : enabledExtensions) {
				if (strcmp(name, extensionName) == 0) { return true; }
			}
			Log::Debug("Enabling device extension '{}'.", extensionName);
			enabledExtensions.push_back(extensionName);

			return true;
		};
		for (const auto& name : requiredExtensions) {
			if (!TryExtension(name)) {
				throw vk::ExtensionNotPresentError("Extension " + std::string(name) + " was not available!");
			}
		}

		// If this extension is available, it is REQUIRED to enable.
		TryExtension("VK_KHR_portability_subset");

		_extensions.TimelineSemaphore = TryExtension(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
	}

	// Find and assign all of our queues.
	auto familyProps = _gpuInfo.QueueFamilies;
	std::vector<std::vector<float>> familyPriorities(familyProps.size());
	std::vector<vk::DeviceQueueCreateInfo> queueCIs(QueueTypeCount);
	{
		std::vector<uint32_t> nextFamilyIndex(familyProps.size(), 0);

		// Assign each of our Graphics, Compute, and Transfer queues. Prefer finding separate queues for
		// each if at all possible.
		const auto AssignQueue = [&](QueueType type, vk::QueueFlags required, vk::QueueFlags ignored) -> bool {
			for (size_t q = 0; q < familyProps.size(); ++q) {
				auto& family = familyProps[q];
				if (family.queueFlags & ignored || family.queueCount == 0) { continue; }

				// Require presentation support for our graphics queue.
				if (type == QueueType::Graphics) {
					const auto present = _gpu.getSurfaceSupportKHR(q, _surface);
					if (!present) { continue; }
				}

				_queues.Family(type) = q;
				_queues.Index(type)  = nextFamilyIndex[q]++;
				--family.queueCount;
				familyPriorities[q].push_back(1.0f);

				Log::Debug("Using queue {}.{} for {}.", _queues.Family(type), _queues.Index(type), QueueTypeName(type));

				return true;
			}

			return false;
		};

		// First find our main Graphics queue.
		if (!AssignQueue(QueueType::Graphics, vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute, {})) {
			throw vk::IncompatibleDriverError("Could not find a suitable graphics/compute queue!");
		}

		// Then, attempt to find a dedicated compute queue, or any unused queue with compute. Fall back
		// to sharing with graphics.
		if (!AssignQueue(QueueType::Compute, vk::QueueFlagBits::eCompute, vk::QueueFlagBits::eGraphics) &&
		    !AssignQueue(QueueType::Compute, vk::QueueFlagBits::eCompute, {})) {
			_queues.Family(QueueType::Compute) = _queues.Family(QueueType::Graphics);
			_queues.Index(QueueType::Compute)  = _queues.Index(QueueType::Graphics);
			Log::Debug("Sharing Compute queue with Graphics.");
		}

		// Finally, attempt to find a dedicated transfer queue. Try to avoid graphics/compute, then
		// compute, then just take what we can. Fall back to sharing with compute.
		if (!AssignQueue(QueueType::Transfer,
		                 vk::QueueFlagBits::eTransfer,
		                 vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute) &&
		    !AssignQueue(QueueType::Transfer, vk::QueueFlagBits::eTransfer, vk::QueueFlagBits::eCompute) &&
		    !AssignQueue(QueueType::Transfer, vk::QueueFlagBits::eTransfer, {})) {
			_queues.Family(QueueType::Transfer) = _queues.Family(QueueType::Compute);
			_queues.Index(QueueType::Transfer)  = _queues.Index(QueueType::Compute);
			Log::Debug("Sharing Transfer queue with Compute.");
		}

		uint32_t familyCount = 0;
		uint32_t queueCount  = 0;
		for (uint32_t i = 0; i < familyProps.size(); ++i) {
			if (nextFamilyIndex[i] > 0) {
				queueCount += nextFamilyIndex[i];
				queueCIs[familyCount++] = vk::DeviceQueueCreateInfo({}, i, nextFamilyIndex[i], familyPriorities[i].data());
			}
		}
		queueCIs.resize(familyCount);
		Log::Trace("Creating {} queues on {} unique families.", queueCount, familyCount);
	}

	// Determine what features we want to enable.
	vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceTimelineSemaphoreFeatures> enabledFeaturesChain;
	{
		auto& features = enabledFeaturesChain.get<vk::PhysicalDeviceFeatures2>().features;
		if (_gpuInfo.AvailableFeatures.Features.samplerAnisotropy == VK_TRUE) {
			Log::Trace("Enabling Sampler Anisotropy (x{}).", _gpuInfo.Properties.Properties.limits.maxSamplerAnisotropy);
			features.samplerAnisotropy = VK_TRUE;
		}

		if (_extensions.TimelineSemaphore) {
			auto& timelineSemaphore = enabledFeaturesChain.get<vk::PhysicalDeviceTimelineSemaphoreFeatures>();
			if (_gpuInfo.AvailableFeatures.TimelineSemaphore.timelineSemaphore == VK_TRUE) {
				Log::Trace("Enabling Timeline Semaphores.");
				timelineSemaphore.timelineSemaphore = VK_TRUE;
			}
		} else {
			enabledFeaturesChain.unlink<vk::PhysicalDeviceTimelineSemaphoreFeatures>();
		}

		_gpuInfo.EnabledFeatures.Features = features;
		_gpuInfo.EnabledFeatures.TimelineSemaphore =
			enabledFeaturesChain.get<vk::PhysicalDeviceTimelineSemaphoreFeatures>();
	}

	// Create our device.
	const vk::DeviceCreateInfo deviceCI(
		{},
		queueCIs,
		nullptr,
		enabledExtensions,
		_extensions.GetPhysicalDeviceProperties2 ? nullptr : &_gpuInfo.EnabledFeatures.Features);
	vk::StructureChain<vk::DeviceCreateInfo, vk::PhysicalDeviceFeatures2> chain(deviceCI, enabledFeaturesChain.get());
	if (!_extensions.GetPhysicalDeviceProperties2) { chain.unlink<vk::PhysicalDeviceFeatures2>(); }

	_device = _gpu.createDevice(chain.get());
	VULKAN_HPP_DEFAULT_DISPATCHER.init(_device);

	// Fetch our created queues.
	for (int q = 0; q < QueueTypeCount; ++q) {
		if (_queues.Families[q] != VK_QUEUE_FAMILY_IGNORED && _queues.Indices[q] != VK_QUEUE_FAMILY_IGNORED) {
			_queues.Queues[q] = _device.getQueue(_queues.Families[q], _queues.Indices[q]);
		}
	}
}

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
	Log::Trace("- Device API Version: {}.{}.{}",
	           VK_API_VERSION_MAJOR(_gpuInfo.Properties.Properties.apiVersion),
	           VK_API_VERSION_MINOR(_gpuInfo.Properties.Properties.apiVersion),
	           VK_API_VERSION_PATCH(_gpuInfo.Properties.Properties.apiVersion));
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

	Log::Trace("- Features:");
	Log::Trace("  - Sampler Anisotropy: {}", _gpuInfo.AvailableFeatures.Features.samplerAnisotropy == VK_TRUE);
	Log::Trace("  - Timeline Semaphores: {}", _gpuInfo.AvailableFeatures.TimelineSemaphore.timelineSemaphore == VK_TRUE);

	Log::Trace("----- End Vulkan Physical Device Info -----");
}
}  // namespace Vulkan
}  // namespace Luna
