#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <Luna/Application/GlfwPlatform.hpp>
#include <Luna/Utility/Log.hpp>
#include <algorithm>
#include <stdexcept>

namespace Luna {
GlfwPlatform::GlfwPlatform() {
	if (glfwInit() != GLFW_TRUE) { throw std::runtime_error("Failed to initialize GLFW!"); }
}

GlfwPlatform::~GlfwPlatform() noexcept {
	glfwTerminate();
}

glm::uvec2 GlfwPlatform::GetFramebufferSize() const {
	if (_window) {
		int width, height;
		glfwGetFramebufferSize(_window, &width, &height);

		return {width, height};
	}

	return {0, 0};
}

std::vector<const char*> GlfwPlatform::GetRequiredDeviceExtensions() const {
	return {"VK_KHR_swapchain"};
}

std::vector<const char*> GlfwPlatform::GetRequiredInstanceExtensions() const {
	uint32_t extensionCount = 0;
	const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);

	return std::vector<const char*>(extensions, extensions + extensionCount);
}

bool GlfwPlatform::IsAlive() const {
	if (_window) { return !glfwWindowShouldClose(_window); }

	return false;
}

VkSurfaceKHR GlfwPlatform::CreateSurface(VkInstance instance) {
	VkSurfaceKHR surface;
	const VkResult result = glfwCreateWindowSurface(instance, _window, nullptr, &surface);
	if (result != VK_SUCCESS) { return VK_NULL_HANDLE; }

	Log::Debug("Vulkan", "Surface created.");

	return surface;
}

void GlfwPlatform::Initialize() {
	int windowWidth  = 1600;
	int windowHeight = 900;

	GLFWmonitor* monitor         = glfwGetPrimaryMonitor();
	const GLFWvidmode* videoMode = glfwGetVideoMode(monitor);
	windowWidth                  = std::min(windowWidth, videoMode->width);
	windowHeight                 = std::min(windowHeight, videoMode->height);

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	if (windowWidth == videoMode->width && windowHeight == videoMode->height) {
		glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
	}
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

	Log::Debug("GlfwPlatform", "Creating main window @ {}x{}", windowWidth, windowHeight);
	_window = glfwCreateWindow(1600, 900, "Luna", nullptr, nullptr);
	if (!_window) { throw std::runtime_error("Failed to create GLFW window!"); }

	if (glfwGetWindowAttrib(_window, GLFW_MAXIMIZED) != GLFW_TRUE) {
		glfwGetWindowSize(_window, &windowWidth, &windowHeight);
		int windowX = (videoMode->width - windowWidth) / 2;
		int windowY = (videoMode->height - windowHeight) / 2;
		glfwSetWindowPos(_window, windowX, windowY);
	}

	glfwSetWindowUserPointer(_window, this);
	glfwShowWindow(_window);
	glfwFocusWindow(_window);
}

void GlfwPlatform::Update() {
	if (_window) { glfwPollEvents(); }
}

void GlfwPlatform::Shutdown() {
	if (_window) { glfwDestroyWindow(_window); }
}
}  // namespace Luna
