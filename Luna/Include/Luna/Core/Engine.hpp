#pragma once

namespace Luna {
class Scene;
class Window;

struct EngineOptions {};

class Engine final {
 public:
	static bool Initialize(const EngineOptions& options);
	static int Run();
	static void Shutdown();

	static Scene& GetActiveScene();
	static Window* GetMainWindow();
};
}  // namespace Luna
