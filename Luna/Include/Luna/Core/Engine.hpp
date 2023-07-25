#pragma once

#include <Luna/Common.hpp>

namespace Luna {
class Window;

class Engine final {
 public:
	static bool Initialize();
	static int Run();
	static void Shutdown();

	static double GetTime();
	static Window* GetMainWindow();
};
}  // namespace Luna
