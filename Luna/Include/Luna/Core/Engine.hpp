#pragma once

namespace Luna {
class Window;

struct EngineOptions {};

class Engine final {
 public:
	static bool Initialize(const EngineOptions& options);
	static int Run();
	static void Shutdown();

	static Window* GetMainWindow();
};
}  // namespace Luna
