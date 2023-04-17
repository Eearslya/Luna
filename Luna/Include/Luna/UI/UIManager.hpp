#pragma once

using ImTextureID = void*;

namespace Luna {
namespace Vulkan {
class CommandBuffer;
}  // namespace Vulkan

struct UITexture;

class UIManager {
 public:
	static bool Initialize();
	static void Shutdown();

	static void BeginFrame(double deltaTime);
	static void Render(Vulkan::CommandBuffer& cmd);
	static ImTextureID SceneView(int view);
	static void UpdateFontAtlas();
};
}  // namespace Luna
