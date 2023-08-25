#pragma once

#include <Luna/Renderer/Common.hpp>

namespace Luna {
class Renderer final {
 public:
	static bool Initialize();
	static void Shutdown();

	static Vulkan::Device& GetDevice();
	static void Render();
};
}  // namespace Luna
