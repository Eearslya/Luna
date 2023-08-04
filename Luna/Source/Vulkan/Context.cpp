#include <Luna/Core/Threading.hpp>
#include <Luna/Vulkan/Context.hpp>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE;

namespace Luna {
namespace Vulkan {
#ifdef LUNA_VULKAN_DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                          VkDebugUtilsMessageTypeFlagsEXT type,
                                                          const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                          void* userData) {
	// UNASSIGNED-BestPractices-vkCreateInstance-specialuse-extension-debugging
	if (data->messageIdNumber == 0x822806fa) { return VK_FALSE; }

	switch (severity) {
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
			Log::Error("Vulkan", "Vulkan ERROR: {}", data->pMessage);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
			Log::Warning("Vulkan", "Vulkan Warning: {}", data->pMessage);
			break;
		default:
			Log::Debug("Vulkan", "Vulkan: {}", data->pMessage);
			break;
	}

	if (data->messageIdNumber == 0x141cb623) [[unlikely]] {
		try {
			const std::string message      = data->pMessage;
			const auto firstThreadStart    = message.find("in thread ") + 10;
			const auto firstThreadEnd      = message.find(" ", firstThreadStart);
			const auto secondThreadStart   = message.find("thread ", firstThreadEnd) + 7;
			const std::string firstThread  = message.substr(firstThreadStart, firstThreadEnd - firstThreadStart);
			const std::string secondThread = message.substr(secondThreadStart);
			const uint32_t firstIndex      = Threading::GetThreadIDFromSys(firstThread);
			const uint32_t secondIndex     = Threading::GetThreadIDFromSys(secondThread);

			const auto LogThread = [](const std::string& id, uint32_t index) {
				if (index == 0) {
					Log::Error("Vulkan", "- Thread {} is main thread", id);
				} else if (index == std::numeric_limits<uint32_t>::max()) {
					Log::Error("Vulkan", "- Thread {} is not one of ours", id);
				} else {
					Log::Error("Vulkan", "- Thread {} is worker thread {}", id, index);
				}
			};
			LogThread(firstThread, firstIndex);
			LogThread(secondThread, secondIndex);
		} catch (const std::exception& e) { return VK_FALSE; }
	}

	return VK_FALSE;
}
#endif

Context::Context(const std::vector<const char*>& instanceExtensions, const std::vector<const char*>& deviceExtensions) {
	if (!_loader.success()) { throw std::runtime_error("Failed to load Vulkan loader"); }

	VULKAN_HPP_DEFAULT_DISPATCHER.init(_loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));

#ifdef LUNA_VULKAN_DEBUG
	DumpInstanceInfo();
#endif
	CreateInstance(instanceExtensions);

	SelectPhysicalDevice(deviceExtensions);
#ifdef LUNA_VULKAN_DEBUG
	DumpDeviceInfo();
#endif
	CreateDevice(deviceExtensions);
}

Context::~Context() noexcept {
	if (_device) {
		_device.waitIdle();
		_device.destroy();
	}

	if (_instance) {
#ifdef LUNA_VULKAN_DEBUG
		if (_debugMessenger) { _instance.destroyDebugUtilsMessengerEXT(_debugMessenger); }
#endif
		_instance.destroy();
	}
}

void Context::CreateInstance(const std::vector<const char*>& requiredExtensions) {
	struct Extension {
		std::string Name;
		uint32_t Version;
		std::string Layer;
	};

	const auto vulkanVersion = vk::enumerateInstanceVersion();
	if (vulkanVersion < TargetVulkanVersion) {
		throw std::runtime_error("System does not support required Vulkan version");
	}

	const auto availableLayers = vk::enumerateInstanceLayerProperties();
	std::vector<const char*> enabledLayers;

	std::unordered_map<std::string, Extension> availableExtensions;
	std::vector<const char*> enabledExtensions;

	const auto EnumerateExtensions = [&](const vk::LayerProperties* layer) -> void {
		const std::string layerName = layer ? std::string(layer->layerName.data()) : "";
		const vk::Optional<const std::string> vkLayerName(layer ? &layerName : nullptr);
		const auto extensions = vk::enumerateInstanceExtensionProperties(vkLayerName);

		for (const auto& extension : extensions) {
			const std::string name(extension.extensionName.data());
			Extension ext{name, extension.specVersion, layerName};

			auto it = availableExtensions.find(name);
			if (it == availableExtensions.end() || it->second.Version < ext.Version) { availableExtensions[name] = ext; }
		}
	};
	EnumerateExtensions(nullptr);
	for (const auto& layer : availableLayers) { EnumerateExtensions(&layer); }

	const auto HasLayer = [&availableLayers](const char* layerName) -> bool {
		return std::find_if(
						 availableLayers.begin(), availableLayers.end(), [layerName](const vk::LayerProperties& layer) -> bool {
							 return strcmp(layer.layerName, layerName) == 0;
						 }) != availableLayers.end();
	};
	const auto TryLayer = [&](const char* layerName) -> bool {
		if (!HasLayer(layerName)) { return false; }
		for (const auto& name : enabledLayers) {
			if (strcmp(name, layerName) == 0) { return true; }
		}

		Log::Trace("Vulkan::Context", "Enabling instance layer '{}'.", layerName);
		enabledLayers.push_back(layerName);

		return true;
	};

	const auto HasExtension = [&availableExtensions](const char* extName) -> bool {
		return availableExtensions.find(std::string(extName)) != availableExtensions.end();
	};
	const auto TryExtension = [&](const char* extName) -> bool {
		if (!HasExtension(extName)) { return false; }
		for (const auto& name : enabledExtensions) {
			if (strcmp(name, extName) == 0) { return true; }
		}

		const auto& ext = availableExtensions.at(std::string(extName));
		if (!ext.Layer.empty() && !TryLayer(ext.Layer.c_str())) { return false; }

		Log::Trace("Vulkan::Context", "Enabling instance extension '{}'.", extName);
		enabledExtensions.push_back(extName);

		return true;
	};

	for (const auto& ext : requiredExtensions) {
		if (!TryExtension(ext)) {
			Log::Fatal("Vulkan::Context", "Required instance extension '{}' could not be enabled", ext);
			throw std::runtime_error("Failed to enable required instance extensions");
		}
	}

	_extensions.DebugUtils = TryExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	_extensions.Surface    = TryExtension(VK_KHR_SURFACE_EXTENSION_NAME);
	_extensions.GetSurfaceCapabilities2 =
		_extensions.Surface && TryExtension(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
	_extensions.SurfaceMaintenance1 =
		_extensions.Surface && _extensions.GetSurfaceCapabilities2 && TryExtension("VK_EXT_surface_maintenance1");
	_extensions.SwapchainColorspace = _extensions.Surface && TryExtension(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);

#ifdef LUNA_VULKAN_DEBUG
	TryLayer("VK_LAYER_KHRONOS_validation");
	_extensions.ValidationFeatures = TryExtension(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
#endif

	const vk::ApplicationInfo appInfo(
		"Luna", VK_MAKE_API_VERSION(0, 1, 0, 0), "Luna", VK_MAKE_API_VERSION(0, 1, 0, 0), TargetVulkanVersion);
	const vk::InstanceCreateInfo instanceCI({}, &appInfo, enabledLayers, enabledExtensions);

#ifdef LUNA_VULKAN_DEBUG
	const vk::DebugUtilsMessengerCreateInfoEXT debugCI(
		{},
		vk::DebugUtilsMessageSeverityFlagBitsEXT::eError | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning,
		vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
			vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
		VulkanDebugCallback,
		this);

	const std::vector<vk::ValidationFeatureEnableEXT> validationEnable = {
		vk::ValidationFeatureEnableEXT::eBestPractices, vk::ValidationFeatureEnableEXT::eSynchronizationValidation};
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

	Log::Trace("Vulkan", "Instance created.");
	VULKAN_HPP_DEFAULT_DISPATCHER.init(_instance);

#ifdef LUNA_VULKAN_DEBUG
	if (_extensions.DebugUtils) {
		_debugMessenger = _instance.createDebugUtilsMessengerEXT(debugCI);
		Log::Trace("Vulkan", "Debug Messenger created.");
	}
#endif
}

void Context::SelectPhysicalDevice(const std::vector<const char*>& requiredExtensions) {
	const auto physicalDevices = _instance.enumeratePhysicalDevices();
	if (physicalDevices.empty()) { throw std::runtime_error("No Vulkan devices available"); }

	const auto HasExtension = [](const DeviceInfo& info, const char* extensionName) -> bool {
		return std::find_if(info.AvailableExtensions.begin(),
		                    info.AvailableExtensions.end(),
		                    [extensionName](const vk::ExtensionProperties& ext) -> bool {
													return strcmp(ext.extensionName, extensionName) == 0;
												}) != info.AvailableExtensions.end();
	};

	std::vector<DeviceInfo> deviceInfos(physicalDevices.size());
	for (size_t i = 0; i < physicalDevices.size(); ++i) {
		auto& info          = deviceInfos[i];
		info.PhysicalDevice = physicalDevices[i];

		info.AvailableExtensions = info.PhysicalDevice.enumerateDeviceExtensionProperties(nullptr);
		info.Memory              = info.PhysicalDevice.getMemoryProperties();
		info.QueueFamilies       = info.PhysicalDevice.getQueueFamilyProperties();

		std::sort(info.AvailableExtensions.begin(), info.AvailableExtensions.end());

		vk::StructureChain<vk::PhysicalDeviceFeatures2,
		                   vk::PhysicalDeviceSynchronization2Features,
		                   vk::PhysicalDeviceVulkan12Features>
			featuresChain;
		vk::StructureChain<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceVulkan12Properties> propertiesChain;

		if (!HasExtension(info, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
			featuresChain.unlink<vk::PhysicalDeviceSynchronization2Features>();
		}

		info.PhysicalDevice.getFeatures2(&featuresChain.get());
		info.PhysicalDevice.getProperties2(&propertiesChain.get());

		info.AvailableFeatures.Core             = featuresChain.get().features;
		info.AvailableFeatures.Synchronization2 = featuresChain.get<vk::PhysicalDeviceSynchronization2Features>();
		info.AvailableFeatures.Vulkan12         = featuresChain.get<vk::PhysicalDeviceVulkan12Features>();

		info.Properties.Core     = propertiesChain.get().properties;
		info.Properties.Vulkan12 = propertiesChain.get<vk::PhysicalDeviceVulkan12Properties>();
	}

	std::stable_partition(deviceInfos.begin(), deviceInfos.end(), [](const DeviceInfo& info) {
		return info.Properties.Core.deviceType == vk::PhysicalDeviceType::eDiscreteGpu;
	});

	Log::Trace("Vulkan::Context", "Candidate Vulkan devices ({}):", deviceInfos.size());
	std::optional<size_t> selectedIndex;
	for (size_t i = 0; i < deviceInfos.size(); ++i) {
		const auto& info = deviceInfos[i];
		std::string reason;

		if (info.Properties.Core.apiVersion < TargetVulkanVersion) {
			reason = "Does not meet minimum Vulkan version requirement";
			continue;
		}

		for (const auto& ext : requiredExtensions) {
			if (!HasExtension(info, ext)) {
				reason = fmt::format("Missing required extension '{}'", ext);
				break;
			}
		}

		if (reason.empty()) {
			Log::Trace("Vulkan::Context", "- {}: Compatible", info.Properties.Core.deviceName);
			if (!selectedIndex.has_value()) { selectedIndex = i; }
		} else {
			Log::Trace("Vulkan::Context", "- {}: Incompatible ({})", info.Properties.Core.deviceName, reason);
		}
	}

	if (!selectedIndex.has_value()) { throw std::runtime_error("No Vulkan devices meet requirements"); }

	_deviceInfo = deviceInfos[*selectedIndex];
	Log::Debug("Vulkan", "Using Vulkan device '{}'", _deviceInfo.Properties.Core.deviceName);
}

void Context::CreateDevice(const std::vector<const char*>& requiredExtensions) {
	std::vector<const char*> enabledExtensions;

	const auto HasExtension = [&](const char* extensionName) -> bool {
		for (const auto& ext : _deviceInfo.AvailableExtensions) {
			if (strcmp(ext.extensionName, extensionName) == 0) { return true; }
		}

		return false;
	};
	const auto TryExtension = [&](const char* extensionName) -> bool {
		if (!HasExtension(extensionName)) { return false; }
		for (const auto& ext : enabledExtensions) {
			if (strcmp(ext, extensionName) == 0) { return true; }
		}

		Log::Trace("Vulkan::Context", "Enabling device extension '{}'.", extensionName);
		enabledExtensions.push_back(extensionName);

		return true;
	};
	for (const auto& ext : requiredExtensions) { TryExtension(ext); }

	_extensions.Maintenance4     = TryExtension(VK_KHR_MAINTENANCE_4_EXTENSION_NAME);
	_extensions.Synchronization2 = TryExtension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);

	auto familyProps = _deviceInfo.QueueFamilies;
	std::vector<std::vector<float>> familyPriorities(familyProps.size());
	std::vector<vk::DeviceQueueCreateInfo> queueCIs(QueueTypeCount);
	std::vector<uint32_t> nextFamilyIndex(familyProps.size(), 0);
	const auto AssignQueue = [&](QueueType type, vk::QueueFlags require, vk::QueueFlags exclude) -> bool {
		for (size_t q = 0; q < familyProps.size(); ++q) {
			auto& family = familyProps[q];
			if ((family.queueFlags & require) != require || family.queueFlags & exclude || family.queueCount == 0) {
				continue;
			}

			_queueInfo.Family(type) = q;
			_queueInfo.Index(type)  = nextFamilyIndex[q]++;
			--family.queueCount;
			familyPriorities[q].push_back(1.0f);

			return true;
		}

		return false;
	};

	AssignQueue(QueueType::Graphics, vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute, {});

	if (!AssignQueue(QueueType::Compute, vk::QueueFlagBits::eCompute, vk::QueueFlagBits::eGraphics) &&
	    !AssignQueue(QueueType::Compute, vk::QueueFlagBits::eCompute, {})) {
		_queueInfo.Family(QueueType::Compute) = _queueInfo.Family(QueueType::Graphics);
		_queueInfo.Index(QueueType::Compute)  = _queueInfo.Index(QueueType::Graphics);
	}

	if (!AssignQueue(QueueType::Transfer,
	                 vk::QueueFlagBits::eTransfer,
	                 vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute) &&
	    !AssignQueue(QueueType::Transfer, vk::QueueFlagBits::eTransfer, vk::QueueFlagBits::eCompute) &&
	    !AssignQueue(QueueType::Graphics, vk::QueueFlagBits::eTransfer, {})) {
		_queueInfo.Family(QueueType::Transfer) = _queueInfo.Family(QueueType::Compute);
		_queueInfo.Index(QueueType::Transfer)  = _queueInfo.Index(QueueType::Compute);
	}

	Log::Trace("Vulkan::Context", "Vulkan device queues:");
	for (size_t i = 0; i < QueueTypeCount; ++i) {
		Log::Trace("Vulkan::Context", "  {}: Queue {}.{}", QueueType(i), _queueInfo.Families[i], _queueInfo.Indices[i]);
	}

	uint32_t familyCount = 0;
	for (uint32_t i = 0; i < familyProps.size(); ++i) {
		if (nextFamilyIndex[i] > 0) {
			queueCIs[familyCount++] = vk::DeviceQueueCreateInfo({}, i, nextFamilyIndex[i], familyPriorities[i].data());
		}
	}
	queueCIs.resize(familyCount);

	const auto& avail = _deviceInfo.AvailableFeatures;
	auto& enable      = _deviceInfo.EnabledFeatures;

#define TryFeature(featureFlag, featureName)                      \
	do {                                                            \
		if (avail.featureFlag == VK_TRUE) {                           \
			enable.featureFlag = VK_TRUE;                               \
			Log::Trace("Vulkan::Context", "Enabling " featureName "."); \
		}                                                             \
	} while (0)

	TryFeature(Core.samplerAnisotropy, "Sampler Anisotropy");
	TryFeature(Synchronization2.synchronization2, "Synchronization 2");
	TryFeature(Vulkan12.scalarBlockLayout, "Scalar Block Layout");
	TryFeature(Vulkan12.timelineSemaphore, "Timeline Semaphores");

#undef TryFeature

	const vk::PhysicalDeviceFeatures2 features2(_deviceInfo.EnabledFeatures.Core);
	vk::StructureChain featuresChain(features2, enable.Synchronization2, enable.Vulkan12);
	if (!_extensions.Synchronization2) { featuresChain.unlink<vk::PhysicalDeviceSynchronization2Features>(); }

	const vk::DeviceCreateInfo deviceCI({}, queueCIs, nullptr, enabledExtensions, nullptr);
	const vk::StructureChain deviceChain(deviceCI, featuresChain.get());

	_device = _deviceInfo.PhysicalDevice.createDevice(deviceChain.get());
	Log::Trace("Vulkan", "Device created.");
	VULKAN_HPP_DEFAULT_DISPATCHER.init(_device);

	for (int q = 0; q < QueueTypeCount; ++q) {
		_queueInfo.Queues[q] = _device.getQueue(_queueInfo.Families[q], _queueInfo.Indices[q]);
	}
}

#ifdef LUNA_VULKAN_DEBUG
void Context::DumpInstanceInfo() const {
	Log::Trace("Vulkan::Context", "=======================================");
	Log::Trace("Vulkan::Context", "===== Vulkan Instance Information =====");
	Log::Trace("Vulkan::Context", "=======================================");

	const Version instanceVersion = vk::enumerateInstanceVersion();
	Log::Trace("Vulkan::Context", "Instance Version: {}", instanceVersion);
	Log::Trace("Vulkan::Context", "");

	auto instanceExtensions = vk::enumerateInstanceExtensionProperties(nullptr);
	std::sort(instanceExtensions.begin(), instanceExtensions.end());
	Log::Trace("Vulkan::Context", "Instance Extensions ({}):", instanceExtensions.size());
	for (const auto& ext : instanceExtensions) {
		Log::Trace("Vulkan::Context", "- {} v{}", ext.extensionName, ext.specVersion);
	}
	Log::Trace("Vulkan::Context", "");

	auto instanceLayers = vk::enumerateInstanceLayerProperties();
	std::sort(instanceLayers.begin(), instanceLayers.end());
	Log::Trace("Vulkan::Context", "Instance Layers ({}):", instanceLayers.size());
	for (const auto& layer : instanceLayers) {
		Log::Trace("Vulkan::Context",
		           "- {} v{} (Vulkan {}) - {}",
		           layer.layerName,
		           layer.implementationVersion,
		           Version(layer.specVersion),
		           layer.description);

		auto layerExtensions = vk::enumerateInstanceExtensionProperties(std::string(layer.layerName.data()));
		std::sort(layerExtensions.begin(), layerExtensions.end());
		for (const auto& ext : layerExtensions) {
			Log::Trace("Vulkan::Context", "  - {} v{}", ext.extensionName, ext.specVersion);
		}
	}

	Log::Trace("Vulkan::Context", "===========================================");
	Log::Trace("Vulkan::Context", "===== End Vulkan Instance Information =====");
	Log::Trace("Vulkan::Context", "===========================================");
}

void Context::DumpDeviceInfo() const {
	Log::Trace("Vulkan::Context", "=====================================");
	Log::Trace("Vulkan::Context", "===== Vulkan Device Information =====");
	Log::Trace("Vulkan::Context", "=====================================");

	Log::Trace("Vulkan::Context", "Device Name: {}", _deviceInfo.Properties.Core.deviceName);
	Log::Trace("Vulkan::Context", "Device Type: {}", vk::to_string(_deviceInfo.Properties.Core.deviceType));
	Log::Trace("Vulkan::Context", "Vulkan Version: {}", Version(_deviceInfo.Properties.Core.apiVersion));

	Log::Trace("Vulkan::Context", "");

	Log::Trace(
		"Vulkan::Context", "Max Push Constant Size: {}", Size(_deviceInfo.Properties.Core.limits.maxPushConstantsSize));
	Log::Trace(
		"Vulkan::Context", "Max Sampler Anisotropy: {:.1f}", _deviceInfo.Properties.Core.limits.maxSamplerAnisotropy);
	Log::Trace("Vulkan::Context",
	           "Line Width: {:.1f} - {:.1f}",
	           _deviceInfo.Properties.Core.limits.lineWidthRange[0],
	           _deviceInfo.Properties.Core.limits.lineWidthRange[1]);

	Log::Trace("Vulkan::Context", "");

	Log::Trace("Vulkan::Context", "Device Extensions ({}):", _deviceInfo.AvailableExtensions.size());
	for (const auto& ext : _deviceInfo.AvailableExtensions) {
		Log::Trace("Vulkan::Context", "- {} v{}", ext.extensionName, ext.specVersion);
	}

	Log::Trace("Vulkan::Context", "");

	Log::Trace("Vulkan::Context", "Memory Heaps ({}):", _deviceInfo.Memory.memoryHeapCount);
	for (uint32_t i = 0; i < _deviceInfo.Memory.memoryHeapCount; ++i) {
		const auto& heap = _deviceInfo.Memory.memoryHeaps[i];
		Log::Trace("Vulkan::Context", "  {:>2}: {} {}", i, Size(heap.size), vk::to_string(heap.flags));
	}

	Log::Trace("Vulkan::Context", "");

	Log::Trace("Vulkan::Context", "Memory Types ({}):", _deviceInfo.Memory.memoryTypeCount);
	for (uint32_t i = 0; i < _deviceInfo.Memory.memoryTypeCount; ++i) {
		const auto& type = _deviceInfo.Memory.memoryTypes[i];
		Log::Trace("Vulkan::Context", "  {:>2}: Heap {} {}", i, type.heapIndex, vk::to_string(type.propertyFlags));
	}

	Log::Trace("Vulkan::Context", "");

	Log::Trace("Vulkan::Context", "Queue Families ({}):", _deviceInfo.QueueFamilies.size());
	for (uint32_t i = 0; i < _deviceInfo.QueueFamilies.size(); ++i) {
		const auto& family = _deviceInfo.QueueFamilies[i];
		Log::Trace("Vulkan::Context",
		           "  {:>2}: {} Queues {}, {} Timestamp Bits, {}x{}x{} Transfer Granularity",
		           i,
		           family.queueCount,
		           vk::to_string(family.queueFlags),
		           family.timestampValidBits,
		           family.minImageTransferGranularity.width,
		           family.minImageTransferGranularity.height,
		           family.minImageTransferGranularity.depth);
	}

	Log::Trace("Vulkan::Context", "=========================================");
	Log::Trace("Vulkan::Context", "===== End Vulkan Device Information =====");
	Log::Trace("Vulkan::Context", "=========================================");
}
#endif
}  // namespace Vulkan
}  // namespace Luna
