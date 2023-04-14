#pragma once

namespace Luna {
namespace Vulkan {
class CommandBuffer;
}

class UIManager {
 public:
	static bool Initialize();
	static void Shutdown();

	static void BeginFrame(double deltaTime);
	static void EndFrame();
	static void Render(Vulkan::CommandBuffer& cmd);
	static void UpdateFontAtlas();
};
}  // namespace Luna
