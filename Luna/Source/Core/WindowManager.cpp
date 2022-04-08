#include <GLFW/glfw3.h>

#include <Luna/Core/Core.hpp>
#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Log.hpp>
#include <Luna/Core/WindowManager.hpp>
#include <stdexcept>

namespace Luna {
WindowManager* WindowManager::_instance = nullptr;

WindowManager* WindowManager::Get() {
	return _instance;
}

WindowManager::WindowManager() {
	Log::Debug("WindowManager", "Initializing WindowManager module.");

	_instance = this;

	if (glfwInit() != GLFW_TRUE) {
		Log::Fatal("WindowManager", "GLFW failed to initialize!");
		throw std::runtime_error("GLFW failed to initialize!");
	}
	Log::Trace("WindowManager", "GLFW initialized.");

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	_window = glfwCreateWindow(1600, 900, "Luna", nullptr, nullptr);
}

WindowManager::~WindowManager() noexcept {
	Log::Debug("WindowManager", "Shutting down WindowManager module.");

	if (_window) { glfwDestroyWindow(_window); }
	glfwTerminate();
	_instance = nullptr;
}

void WindowManager::Update() {
	glfwPollEvents();

	if (glfwWindowShouldClose(_window)) { Engine::Get()->RequestShutdown(); }
}
}  // namespace Luna
