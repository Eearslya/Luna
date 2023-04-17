#pragma once

namespace Luna {
namespace Vulkan {
class Device;
class ImageView;
}  // namespace Vulkan
class Scene;
class Window;

class Renderer final {
 public:
	static bool Initialize();
	static void Shutdown();

	static Vulkan::Device& GetDevice();
	static const Vulkan::ImageView& GetSceneView(int view);
	static int RegisterSceneView(int width, int height);
	static void Render(double deltaTime);
};
}  // namespace Luna
