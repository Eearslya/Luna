#pragma once

namespace Luna {
namespace Vulkan {
class Device;
}
class Scene;
class Window;

class Renderer final {
 public:
	static bool Initialize();
	static void Shutdown();

	static Vulkan::Device& GetDevice();
	static void Render(double deltaTime);
	static void SetMainWindow(Window& window);
	static void SetScene(const Scene& scene);
};
}  // namespace Luna
