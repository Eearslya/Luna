#pragma once

#include <memory>

namespace Luna {
class WindowManager;

namespace Graphics {
class GraphicsManager;
}

class Engine final {
 public:
	Engine();
	~Engine() noexcept;

	void RequestShutdown();
	int Run();

	static Engine* Get();

 private:
	static Engine* _instance;

	bool _running = false;
	std::unique_ptr<WindowManager> _windowManager;
	std::unique_ptr<Graphics::GraphicsManager> _graphicsManager;
};
}  // namespace Luna
