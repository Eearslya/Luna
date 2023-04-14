#pragma once

#include <vector>

namespace Luna {
class WindowManager final {
 public:
	static bool Initialize();
	static void Update();
	static void Shutdown();

	static std::vector<const char*> GetRequiredInstanceExtensions();
	static double GetTime();
};
}  // namespace Luna
