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

InputAction GlfwPlatform::GetButton(MouseButton button) const {
	return static_cast<InputAction>(glfwGetMouseButton(_window, int(button)));
}

glm::uvec2 GlfwPlatform::GetFramebufferSize() const {
	if (_window) {
		int width, height;
		glfwGetFramebufferSize(_window, &width, &height);

		return {width, height};
	}

	return {0, 0};
}

InputAction GlfwPlatform::GetKey(Key key) const {
	return static_cast<InputAction>(glfwGetKey(_window, int(key)));
}

std::vector<const char*> GlfwPlatform::GetRequiredDeviceExtensions() const {
	return {"VK_KHR_swapchain"};
}

std::vector<const char*> GlfwPlatform::GetRequiredInstanceExtensions() const {
	uint32_t extensionCount = 0;
	const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);

	return std::vector<const char*>(extensions, extensions + extensionCount);
}

double GlfwPlatform::GetTime() const {
	return glfwGetTime();
}

glm::uvec2 GlfwPlatform::GetWindowSize() const {
	if (_window) {
		int width, height;
		glfwGetWindowSize(_window, &width, &height);

		return {width, height};
	}

	return {0, 0};
}

bool GlfwPlatform::IsAlive() const {
	if (_window) { return !glfwWindowShouldClose(_window); }

	return false;
}

VkSurfaceKHR GlfwPlatform::CreateSurface(VkInstance instance) {
	VkSurfaceKHR surface;
	const VkResult result = glfwCreateWindowSurface(instance, _window, nullptr, &surface);
	if (result != VK_SUCCESS) { return VK_NULL_HANDLE; }

	Log::Trace("Vulkan", "Surface created.");

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
	glfwSetMouseButtonCallback(_window, CallbackButton);
	glfwSetCharCallback(_window, CallbackChar);
	glfwSetKeyCallback(_window, CallbackKey);
	glfwSetCursorPosCallback(_window, CallbackPosition);
	glfwSetScrollCallback(_window, CallbackScroll);

	glfwShowWindow(_window);
	glfwFocusWindow(_window);
}

void GlfwPlatform::Update() {
	if (_window) { glfwPollEvents(); }
}

void GlfwPlatform::Shutdown() {
	if (_window) { glfwDestroyWindow(_window); }
}

void GlfwPlatform::CallbackButton(GLFWwindow* window, int32_t button, int32_t action, int32_t mods) {
	Input::MouseButtonEvent(static_cast<MouseButton>(button), static_cast<InputAction>(action), InputMods(mods));
}

void GlfwPlatform::CallbackChar(GLFWwindow* window, uint32_t codepoint) {
	Input::CharEvent(codepoint);
}

void GlfwPlatform::CallbackKey(GLFWwindow* window, int32_t key, int32_t scancode, int32_t action, int32_t mods) {
	Input::KeyEvent(static_cast<Key>(key), static_cast<InputAction>(action), InputMods(mods));
}

void GlfwPlatform::CallbackPosition(GLFWwindow* window, double x, double y) {
	Input::MouseMovedEvent({x, y});
}

void GlfwPlatform::CallbackScroll(GLFWwindow* window, double xOffset, double yOffset) {
	Input::MouseScrolledEvent({xOffset, yOffset});
}
}  // namespace Luna
