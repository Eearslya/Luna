#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <Luna/Application/GlfwPlatform.hpp>
#include <stdexcept>

namespace Luna {
GlfwPlatform::GlfwPlatform(const std::string& name, const glm::uvec2& startSize) : _windowSize(startSize) {
	_shutdownRequested.store(false);

	if (glfwInit() != GLFW_TRUE) { throw std::runtime_error("Failed to initialize GLFW!"); }

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	const std::string windowName = name.empty() ? "Luna" : name;
	_window                      = glfwCreateWindow(_windowSize.x, _windowSize.y, windowName.c_str(), nullptr, nullptr);
	if (_window == nullptr) { throw std::runtime_error("Failed to create GLFW window!"); }

	glfwSetWindowUserPointer(_window, this);
	glfwShowWindow(_window);
	glfwFocusWindow(_window);
}

glm::uvec2 GlfwPlatform::GetFramebufferSize() const {
	glm::ivec2 size(0);
	glfwGetFramebufferSize(_window, &size.x, &size.y);

	return size;
}

std::vector<const char*> GlfwPlatform::GetRequiredInstanceExtensions() const {
	uint32_t extensionCount = 0;
	const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);

	return std::vector<const char*>(extensions, extensions + extensionCount);
}

std::vector<const char*> GlfwPlatform::GetRequiredDeviceExtensions() const {
	return {"VK_KHR_swapchain"};
}

vk::SurfaceKHR GlfwPlatform::CreateSurface(vk::Instance instance) {
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	const VkResult res   = glfwCreateWindowSurface(instance, _window, nullptr, &surface);
	if (res != VK_SUCCESS) { throw std::runtime_error("Failed to create Window surface!"); }

	return surface;
}

bool GlfwPlatform::IsAlive() const {
	return !_shutdownRequested.load();
}

void GlfwPlatform::Update() {
	glfwPollEvents();
	if (glfwWindowShouldClose(_window)) { _shutdownRequested.store(true); }
}
}  // namespace Luna
