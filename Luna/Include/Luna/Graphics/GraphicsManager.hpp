#pragma once

#include <memory>

namespace Luna {
class Engine;

namespace Graphics {
namespace Vulkan {
class Device;
}

class GraphicsManager final {
	friend class Luna::Engine;

 public:
	~GraphicsManager() noexcept;

	void Render();

	static GraphicsManager* Get();

 private:
	static GraphicsManager* _instance;

	GraphicsManager();

	std::unique_ptr<Vulkan::Device> _device;
};
}  // namespace Graphics
}  // namespace Luna
