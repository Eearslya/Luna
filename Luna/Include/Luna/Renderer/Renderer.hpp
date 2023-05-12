#pragma once

#include <Luna/Renderer/Common.hpp>

namespace Luna {
class Camera;
class EditorCamera;
class Scene;
class Window;

class Renderer final {
 public:
	static bool Initialize();
	static void Shutdown();

	static DefaultImages& GetDefaultImages();
	static Vulkan::Device& GetDevice();
	static RenderRunner& GetRunner(RendererSuiteType type);
	static const Vulkan::ImageView& GetSceneView(int view);
	static int RegisterSceneView();
	static void Render(double deltaTime);
	static void UnregisterSceneView(int viewIndex);
	static void UpdateSceneView(int viewIndex, int width, int height, const EditorCamera& camera);
};
}  // namespace Luna
