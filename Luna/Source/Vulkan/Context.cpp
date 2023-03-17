#include <Luna/Utility/Log.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <algorithm>
#include <unordered_map>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE;

namespace Luna {
namespace Vulkan {
#ifdef LUNA_VULKAN_DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                          VkDebugUtilsMessageTypeFlagsEXT type,
                                                          const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                          void* userData) {
	// Ignore "extension should only be used in debug" warning. That's the whole point.
	if (data->messageIdNumber == 0x822806fa) { return VK_FALSE; }

	// TODO: Fix wait stages in submit for created images.
	if (data->messageIdNumber == 0x48a09f6c) { return VK_FALSE; }

	// Pipeline Barriers don't validate sync2 access flags.
	// https://github.com/KhronosGroup/Vulkan-ValidationLayers/issues/5429
	if (data->messageIdNumber == 0x849fcec7) { return VK_FALSE; }

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

	return VK_FALSE;
}
#endif

Context::Context(const std::vector<const char*>& instanceExtensions, const std::vector<const char*>& deviceExtensions) {
	if (!_loader.success()) { throw std::runtime_error("Failed to load Vulkan loader!"); }
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

	const uint32_t vulkanVersion = vk::enumerateInstanceVersion();
	if (vulkanVersion < TargetVulkanVersion) {
		throw std::runtime_error("System does not support required Vulkan version!");
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
			const std::string name = std::string(extension.extensionName.data());
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
			if (strcmp(extName, name) == 0) { return true; }
		}

		const auto& ext = availableExtensions.at(std::string(extName));
		if (!ext.Layer.empty() && !TryLayer(ext.Layer.c_str())) { return false; }

		Log::Trace("Vulkan::Context", "Enabling instance extension '{}'.", extName);
		enabledExtensions.push_back(extName);

		return true;
	};

	for (const auto& ext : requiredExtensions) {
		if (!TryExtension(ext)) {
			Log::Fatal("Vulkan::Context", "Required instance extension '{}' could not be enabled!", ext);
			throw std::runtime_error("Failed to enable required instance extensions!");
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

	Log::Debug("Vulkan", "Instance created.");
	VULKAN_HPP_DEFAULT_DISPATCHER.init(_instance);

#ifdef LUNA_VULKAN_DEBUG
	if (_extensions.DebugUtils) {
		_debugMessenger = _instance.createDebugUtilsMessengerEXT(debugCI);
		Log::Debug("Vulkan", "Debug Messenger created.");
	}
#endif
}

void Context::SelectPhysicalDevice(const std::vector<const char*>& requiredExtensions) {
	const auto physicalDevices = _instance.enumeratePhysicalDevices();
	if (physicalDevices.empty()) { throw std::runtime_error("No Vulkan devices are available on this system!"); }

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

		std::sort(info.AvailableExtensions.begin(),
		          info.AvailableExtensions.end(),
		          [](const vk::ExtensionProperties& a, const vk::ExtensionProperties& b) {
								return std::string(a.extensionName.data()) < std::string(b.extensionName.data());
							});

		vk::StructureChain<vk::PhysicalDeviceFeatures2,
		                   vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
		                   vk::PhysicalDeviceRayTracingPipelineFeaturesKHR,
		                   vk::PhysicalDeviceSynchronization2Features,
		                   vk::PhysicalDeviceVulkan12Features>
			features;
		vk::StructureChain<vk::PhysicalDeviceProperties2,
		                   vk::PhysicalDeviceAccelerationStructurePropertiesKHR,
		                   vk::PhysicalDeviceRayTracingPipelinePropertiesKHR,
		                   vk::PhysicalDeviceVulkan12Properties>
			properties;

		if (!HasExtension(info, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)) {
			features.unlink<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>();
			properties.unlink<vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();
		}
		if (!HasExtension(info, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)) {
			features.unlink<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>();
			properties.unlink<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
		}
		if (!HasExtension(info, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
			features.unlink<vk::PhysicalDeviceSynchronization2Features>();
		}

		info.PhysicalDevice.getFeatures2(&features.get());
		info.PhysicalDevice.getProperties2(&properties.get());

		info.AvailableFeatures.Core                  = features.get().features;
		info.AvailableFeatures.AccelerationStructure = features.get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>();
		info.AvailableFeatures.RayTracingPipeline    = features.get<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>();
		info.AvailableFeatures.Synchronization2      = features.get<vk::PhysicalDeviceSynchronization2Features>();
		info.AvailableFeatures.Vulkan12              = features.get<vk::PhysicalDeviceVulkan12Features>();

		info.Properties.Core                  = properties.get().properties;
		info.Properties.AccelerationStructure = properties.get<vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();
		info.Properties.RayTracingPipeline    = properties.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
		info.Properties.Vulkan12              = properties.get<vk::PhysicalDeviceVulkan12Properties>();
	}

	std::stable_partition(deviceInfos.begin(), deviceInfos.end(), [](const DeviceInfo& info) {
		return info.Properties.Core.deviceType == vk::PhysicalDeviceType::eDiscreteGpu;
	});

	Log::Trace("Vulkan::Context", "Candidate Vulkan Devices ({}):", deviceInfos.size());
	for (const auto& info : deviceInfos) {
		Log::Trace(
			"Vulkan::Context", "- {} ({})", info.Properties.Core.deviceName, vk::to_string(info.Properties.Core.deviceType));
	}

	deviceInfos.erase(std::remove_if(deviceInfos.begin(),
	                                 deviceInfos.end(),
	                                 [&](const DeviceInfo& info) -> bool {
																		 if (info.Properties.Core.apiVersion < TargetVulkanVersion) {
																			 Log::Trace(
																				 "Vulkan::Context",
																				 "Removing candidate '{}': Does not meet minimum Vulkan version requirement.",
																				 info.Properties.Core.deviceName);
																			 return true;
																		 }

																		 for (const auto& ext : requiredExtensions) {
																			 if (!HasExtension(info, ext)) {
																				 Log::Trace("Vulkan::Context",
				                                            "Removing candidate '{}': Missing required extension '{}'.",
				                                            info.Properties.Core.deviceName,
				                                            ext);
																				 return true;
																			 }
																		 }

																		 return false;
																	 }),
	                  deviceInfos.end());
	if (deviceInfos.empty()) { throw std::runtime_error("No Vulkan devices met requirements!"); }

	_deviceInfo = deviceInfos[0];

	Log::Debug("Vulkan", "Using physical device '{}'.", _deviceInfo.Properties.Core.deviceName);
}

void Context::CreateDevice(const std::vector<const char*>& requiredExtensions) {
	std::vector<const char*> enabledExtensions;

	// First enable all required extensions.
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
	for (const auto& ext : requiredExtensions) {
		if (!TryExtension(ext)) {
			// This should never happen, as SelectPhysicalDevice already verifies required extensions.
			throw std::runtime_error("Failed to enable required device extensions!");
		}
	}

	_extensions.DeferredHostOperations = TryExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
	if (_extensions.DeferredHostOperations) {
		_extensions.AccelerationStructure = TryExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
	}
	if (_extensions.AccelerationStructure) {
		_extensions.RayTracingPipeline = TryExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
	}
	_extensions.CalibratedTimestamps = TryExtension(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
	_extensions.Synchronization2     = TryExtension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);

	// Determine our queue family assignments.
	auto familyProps = _deviceInfo.QueueFamilies;
	std::vector<std::vector<float>> familyPriorities(familyProps.size());
	std::vector<vk::DeviceQueueCreateInfo> queueCIs(QueueTypeCount);
	std::vector<uint32_t> nextFamilyIndex(familyProps.size(), 0);
	const auto AssignQueue = [&](QueueType type, vk::QueueFlags required, vk::QueueFlags ignored) -> bool {
		for (size_t q = 0; q < familyProps.size(); ++q) {
			auto& family = familyProps[q];
			if ((family.queueFlags & required) != required || family.queueFlags & ignored || family.queueCount == 0) {
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

	// Assign our Graphics queue to the first queue that supports Graphics and Compute. This type of queue is guaranteed
	// to us by the Vulkan spec.
	AssignQueue(QueueType::Graphics, vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute, {});

	// Attempt to find a queue that supports Compute, but not Graphics.
	// Else, find any unused queue that supports Compute.
	// Else, use our Graphics queue for Compute.
	if (!AssignQueue(QueueType::Compute, vk::QueueFlagBits::eCompute, vk::QueueFlagBits::eGraphics) &&
	    !AssignQueue(QueueType::Compute, vk::QueueFlagBits::eCompute, {})) {
		_queueInfo.Family(QueueType::Compute) = _queueInfo.Family(QueueType::Graphics);
		_queueInfo.Index(QueueType::Compute)  = _queueInfo.Index(QueueType::Graphics);
	}

	// Attempt to find a queue that supports Transfer, but not Graphics or Compute.
	// Else, find a queue that does not support Compute.
	// Else, find any unused queue that supports Transfer.
	// Else, use our Compute queue for Transfer.
	if (!AssignQueue(QueueType::Transfer,
	                 vk::QueueFlagBits::eTransfer,
	                 vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute) &&
	    !AssignQueue(QueueType::Transfer, vk::QueueFlagBits::eTransfer, vk::QueueFlagBits::eCompute) &&
	    !AssignQueue(QueueType::Transfer, vk::QueueFlagBits::eTransfer, {})) {
		_queueInfo.Family(QueueType::Transfer) = _queueInfo.Family(QueueType::Compute);
		_queueInfo.Index(QueueType::Transfer)  = _queueInfo.Index(QueueType::Compute);
	}

	Log::Trace("Vulkan::Context", "Vulkan device queues:");
	for (size_t i = 0; i < QueueTypeCount; ++i) {
		Log::Trace("Vulkan::Context",
		           "\t{}: Queue {}.{}",
		           VulkanEnumToString(static_cast<QueueType>(i)),
		           _queueInfo.Families[i],
		           _queueInfo.Indices[i]);
	}

	// Create the relevant queue creation structs.
	uint32_t familyCount = 0;
	for (uint32_t i = 0; i < familyProps.size(); ++i) {
		if (nextFamilyIndex[i] > 0) {
			queueCIs[familyCount++] = vk::DeviceQueueCreateInfo({}, i, nextFamilyIndex[i], familyPriorities[i].data());
		}
	}
	queueCIs.resize(familyCount);

	// Enable any features we want/need.
	const auto& avail = _deviceInfo.AvailableFeatures;
	auto& enable      = _deviceInfo.EnabledFeatures;

	if (avail.Core.shaderInt64) {
		Log::Trace("Vulkan::Context", "Enabling shader Int64 usage.");
		enable.Core.shaderInt64 = VK_TRUE;
	}
	if (avail.AccelerationStructure.accelerationStructure) {
		Log::Trace("Vulkan::Context", "Enabling Acceleration Structures.");
		enable.AccelerationStructure.accelerationStructure = VK_TRUE;
	}
	if (avail.RayTracingPipeline.rayTracingPipeline) {
		Log::Trace("Vulkan::Context", "Enabling Ray Tracing Pipelines.");
		enable.RayTracingPipeline.rayTracingPipeline = VK_TRUE;
	}
	if (avail.Synchronization2.synchronization2) {
		Log::Trace("Vulkan::Context", "Enabling Synchronization 2.");
		enable.Synchronization2.synchronization2 = VK_TRUE;
	}
	if (avail.Vulkan12.bufferDeviceAddress) {
		Log::Trace("Vulkan::Context", "Enabling Buffer Device Addresses.");
		enable.Vulkan12.bufferDeviceAddress = VK_TRUE;
	}
	if (avail.Vulkan12.hostQueryReset) {
		Log::Trace("Vulkan::Context", "Enabling Host Query Reset.");
		enable.Vulkan12.hostQueryReset = VK_TRUE;
	}
	if (avail.Vulkan12.timelineSemaphore) {
		Log::Trace("Vulkan::Context", "Enabling Timeline Semaphores.");
		enable.Vulkan12.timelineSemaphore = VK_TRUE;
	}

	// Create our device.
	const vk::PhysicalDeviceFeatures2 features2(_deviceInfo.EnabledFeatures.Core);
	const vk::StructureChain<vk::PhysicalDeviceFeatures2,
	                         vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
	                         vk::PhysicalDeviceRayTracingPipelineFeaturesKHR,
	                         vk::PhysicalDeviceSynchronization2Features,
	                         vk::PhysicalDeviceVulkan12Features>
		featuresChain(
			features2, enable.AccelerationStructure, enable.RayTracingPipeline, enable.Synchronization2, enable.Vulkan12);
	const vk::DeviceCreateInfo deviceCI({}, queueCIs, nullptr, enabledExtensions, nullptr);
	const vk::StructureChain<vk::DeviceCreateInfo, vk::PhysicalDeviceFeatures2> deviceChain(deviceCI,
	                                                                                        featuresChain.get());
	_device = _deviceInfo.PhysicalDevice.createDevice(deviceChain.get());
	Log::Debug("Vulkan", "Device created.");
	// Load device function pointers.
	VULKAN_HPP_DEFAULT_DISPATCHER.init(_device);

	// Fetch our device queues.
	for (int q = 0; q < QueueTypeCount; ++q) {
		_queueInfo.Queues[q] = _device.getQueue(_queueInfo.Families[q], _queueInfo.Indices[q]);
	}
}

#ifdef LUNA_VULKAN_DEBUG
void Context::DumpInstanceInfo() const {
	Log::Trace("Vulkan::Context", "----- Vulkan Instance Information -----");

	const auto instanceVersion = vk::enumerateInstanceVersion();
	Log::Trace("Vulkan::Context",
	           "\tInstance Version: {}.{}.{}.{}",
	           VK_API_VERSION_VARIANT(instanceVersion),
	           VK_API_VERSION_MAJOR(instanceVersion),
	           VK_API_VERSION_MINOR(instanceVersion),
	           VK_API_VERSION_PATCH(instanceVersion));

	const auto instanceExtensions = vk::enumerateInstanceExtensionProperties(nullptr);
	Log::Trace("Vulkan::Context", "\tInstance Extensions ({}):", instanceExtensions.size());
	for (const auto& ext : instanceExtensions) {
		Log::Trace("Vulkan::Context", "\t- {} v{}", ext.extensionName, ext.specVersion);
	}

	const auto instanceLayers = vk::enumerateInstanceLayerProperties();
	Log::Trace("Vulkan::Context", "\tInstance Layers ({}):", instanceLayers.size());
	for (const auto& layer : instanceLayers) {
		Log::Trace("Vulkan::Context",
		           "\t- {} v{} (Vulkan {}.{}.{}) - {}",
		           layer.layerName,
		           layer.implementationVersion,
		           VK_API_VERSION_MAJOR(layer.specVersion),
		           VK_API_VERSION_MINOR(layer.specVersion),
		           VK_API_VERSION_PATCH(layer.specVersion),
		           layer.description);

		const auto layerExtensions = vk::enumerateInstanceExtensionProperties(std::string(layer.layerName.data()));
		for (const auto& ext : layerExtensions) {
			Log::Trace("Vulkan::Context", "\t\t- {} v{}", ext.extensionName, ext.specVersion);
		}
	}

	Log::Trace("Vulkan::Context", "----- End Vulkan Instance Information -----");
}

void Context::DumpDeviceInfo() const {
	Log::Trace("Vulkan::Context", "----- Vulkan Device Information -----");

	Log::Trace("Vulkan::Context", "\tDevice Name: {}", _deviceInfo.Properties.Core.deviceName);
	Log::Trace("Vulkan::Context", "\tDevice Type: {}", vk::to_string(_deviceInfo.Properties.Core.deviceType));
	Log::Trace("Vulkan::Context",
	           "\tVulkan Version: {}.{}.{}",
	           VK_API_VERSION_MAJOR(_deviceInfo.Properties.Core.apiVersion),
	           VK_API_VERSION_MINOR(_deviceInfo.Properties.Core.apiVersion),
	           VK_API_VERSION_PATCH(_deviceInfo.Properties.Core.apiVersion));

	Log::Trace("Vulkan::Context", "\tDevice Extensions ({}):", _deviceInfo.AvailableExtensions.size());
	for (const auto& ext : _deviceInfo.AvailableExtensions) {
		Log::Trace("Vulkan::Context", "\t- {} v{}", ext.extensionName, ext.specVersion);
	}

	Log::Trace("Vulkan::Context", "\tMemory Heaps ({}):", _deviceInfo.Memory.memoryHeapCount);
	for (uint32_t i = 0; i < _deviceInfo.Memory.memoryHeapCount; ++i) {
		const auto& heap = _deviceInfo.Memory.memoryHeaps[i];
		Log::Trace("Vulkan::Context", "\t\t{}: {} {}", i, FormatSize(heap.size), vk::to_string(heap.flags));
	}

	Log::Trace("Vulkan::Context", "\tMemory Types ({}):", _deviceInfo.Memory.memoryTypeCount);
	for (uint32_t i = 0; i < _deviceInfo.Memory.memoryTypeCount; ++i) {
		const auto& type = _deviceInfo.Memory.memoryTypes[i];
		Log::Trace("Vulkan::Context", "\t\t{}: Heap {} {}", i, type.heapIndex, vk::to_string(type.propertyFlags));
	}

	Log::Trace("Vulkan::Context", "\tQueue Families ({}):", _deviceInfo.QueueFamilies.size());
	for (size_t i = 0; i < _deviceInfo.QueueFamilies.size(); ++i) {
		const auto& family = _deviceInfo.QueueFamilies[i];
		Log::Trace("Vulkan::Context",
		           "\t\t{}: {} Queues {}, {} Timestamp Bits, {}x{}x{} Transfer Granularity",
		           i,
		           family.queueCount,
		           vk::to_string(family.queueFlags),
		           family.timestampValidBits,
		           family.minImageTransferGranularity.width,
		           family.minImageTransferGranularity.height,
		           family.minImageTransferGranularity.depth);
	}

	Log::Trace("Vulkan::Context", "----- End Vulkan Device Information -----");
}
#endif
}  // namespace Vulkan
}  // namespace Luna
