#include <Luna/Core/Core.hpp>
#include <Luna/Core/Log.hpp>
#include <Luna/Graphics/GraphicsManager.hpp>
#include <Luna/Graphics/Vulkan/Device.hpp>

namespace Luna {
namespace Graphics {
GraphicsManager* GraphicsManager::_instance = nullptr;

GraphicsManager::GraphicsManager() {
	Log::Debug("GraphicsManager", "Initializing GraphicsManager module.");

	_instance = this;

	_device = std::make_unique<Vulkan::Device>();
}

GraphicsManager::~GraphicsManager() noexcept {
	Log::Debug("GraphicsManager", "Shutting down GraphicsManager module.");

	_device.reset();
	_instance = nullptr;
}

void GraphicsManager::Render() {}
}  // namespace Graphics
}  // namespace Luna
