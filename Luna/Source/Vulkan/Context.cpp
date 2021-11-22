#include <Luna/Core/Log.hpp>
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
}

Context::~Context() noexcept {
	if (_instance) {
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
}  // namespace Vulkan
}  // namespace Luna
