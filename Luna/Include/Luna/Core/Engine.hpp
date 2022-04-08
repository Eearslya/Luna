#pragma once

#include <memory>

namespace Luna {
class WindowManager;

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
};
}  // namespace Luna
