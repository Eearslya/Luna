#pragma once

#include <Luna/Common.hpp>

namespace Luna {
namespace Vulkan {
class CommandBuffer;
}

class UIManager {
 public:
	static bool Initialize();
	static void Shutdown();

	static void BeginFrame();
	static void Render(Vulkan::CommandBuffer& cmd);
	static void UpdateFontAtlas();
};
}  // namespace Luna
