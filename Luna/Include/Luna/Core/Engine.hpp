#pragma once

#include <Luna/Common.hpp>

namespace Luna {
class Engine final {
 public:
	static bool Initialize();
	static int Run();
	static void Shutdown();
};
}  // namespace Luna
