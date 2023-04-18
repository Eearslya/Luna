#pragma once

#include <Luna/Utility/Path.hpp>

namespace Luna {
class Editor final {
 public:
	static bool Initialize();
	static void Update(double deltaTime);
	static void Shutdown();

	static void RequestAsset(const Path& assetPath);
};
}  // namespace Luna
