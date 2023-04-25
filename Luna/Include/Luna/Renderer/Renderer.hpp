#pragma once

namespace Luna {
namespace Vulkan {
class Device;
class ImageView;
}  // namespace Vulkan
class Camera;
class EditorCamera;
class Scene;
class Window;

class Renderer final {
 public:
	static bool Initialize();
	static void Shutdown();

	static Vulkan::Device& GetDevice();
	static const Vulkan::ImageView& GetSceneView(int view);
	static int RegisterSceneView();
	static void Render(double deltaTime);
	static void UnregisterSceneView(int viewIndex);
	static void UpdateSceneView(int viewIndex, int width, int height, const EditorCamera& camera);
};
}  // namespace Luna
