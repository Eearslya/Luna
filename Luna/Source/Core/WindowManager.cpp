#include <GLFW/glfw3.h>

#include <Luna/Core/WindowManager.hpp>
#include <Luna/Utility/Log.hpp>
#include <Tracy/Tracy.hpp>

namespace Luna {
static void GlfwErrorCallback(int errorCode, const char* errorDescription) {
	Log::Error("WindowManager", "GLFW Error {}: {}", errorCode, errorDescription);
}

bool WindowManager::Initialize() {
	ZoneScopedN("WindowManager::Initialize");

	glfwSetErrorCallback(GlfwErrorCallback);

	if (!glfwInit()) { return false; }

	return true;
}

void WindowManager::Update() {
	ZoneScopedN("WindowManager::Update");

	glfwPollEvents();
}

void WindowManager::Shutdown() {
	ZoneScopedN("WindowManager::Shutdown");

	glfwTerminate();
}

std::vector<const char*> WindowManager::GetRequiredInstanceExtensions() {
	uint32_t extensionCount = 0;
	const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);

	return std::vector<const char*>(extensions, extensions + extensionCount);
}

double WindowManager::GetTime() {
	return glfwGetTime();
}
}  // namespace Luna
