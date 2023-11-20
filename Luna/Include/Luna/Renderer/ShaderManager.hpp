#pragma once

#include <Luna/Renderer/Common.hpp>

namespace Luna {
class ShaderManager {
	static bool Initialize();
	static void Update();
	static void Shutdown();
};
}  // namespace Luna
