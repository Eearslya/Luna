#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <Luna/Core/Input.hpp>
#include <Luna/Core/Window.hpp>
#include <Luna/Renderer/Swapchain.hpp>
#include <Tracy/Tracy.hpp>
#include <algorithm>
#include <stdexcept>

namespace Luna {
Window::Window(const std::string& title, int width, int height, bool show) {
	ZoneScopedN("Window::Window");

	GLFWmonitor* monitor         = glfwGetPrimaryMonitor();
	const GLFWvidmode* videoMode = glfwGetVideoMode(monitor);
	width                        = std::clamp(width, 1, videoMode->width);
	height                       = std::clamp(height, 1, videoMode->height);

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_MAXIMIZED, width == videoMode->width && height == videoMode->height ? GLFW_TRUE : GLFW_FALSE);
	glfwWindowHint(GLFW_VISIBLE, show ? GLFW_TRUE : GLFW_FALSE);

	_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
	if (!_window) { throw std::runtime_error("Failed to create GLFW window!"); }
	glfwSetWindowUserPointer(_window, this);

	if (glfwGetWindowAttrib(_window, GLFW_MAXIMIZED) == GLFW_FALSE) { CenterPosition(); }

	glfwSetCharCallback(_window, [](GLFWwindow* window, unsigned int c) { Input::CharEvent(c); });
	glfwSetCursorPosCallback(_window, [](GLFWwindow* window, double x, double y) { Input::MouseMovedEvent({x, y}); });
	glfwSetKeyCallback(_window, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
		Input::KeyEvent(Key(key), InputAction(action), InputMods(mods));
	});
	glfwSetMouseButtonCallback(_window, [](GLFWwindow* window, int button, int action, int mods) {
		Input::MouseButtonEvent(MouseButton(button), InputAction(action), InputMods(mods));
	});
	glfwSetScrollCallback(_window, [](GLFWwindow* window, double x, double y) { Input::MouseScrolledEvent({x, y}); });

	_swapchain = MakeHandle<Swapchain>(*this);

	if (show) {
		glfwShowWindow(_window);
		glfwFocusWindow(_window);
	}
}

Window::~Window() noexcept {
	glfwDestroyWindow(_window);
}

VkSurfaceKHR Window::CreateSurface(VkInstance instance) const {
	ZoneScopedN("Window::CreateSurface");

	VkSurfaceKHR surface  = VK_NULL_HANDLE;
	const VkResult result = glfwCreateWindowSurface(instance, _window, nullptr, &surface);
	if (result != VK_SUCCESS) { return VK_NULL_HANDLE; }

	return surface;
}

glm::ivec2 Window::GetFramebufferSize() const {
	glm::ivec2 fbSize(0, 0);
	glfwGetFramebufferSize(_window, &fbSize.x, &fbSize.y);

	return fbSize;
}

GLFWwindow* Window::GetHandle() const {
	return _window;
}

glm::ivec2 Window::GetPosition() const {
	glm::ivec2 pos(0, 0);
	glfwGetWindowPos(_window, &pos.x, &pos.y);

	return pos;
}

glm::ivec2 Window::GetWindowSize() const {
	glm::ivec2 windowSize(0, 0);
	glfwGetWindowSize(_window, &windowSize.x, &windowSize.y);

	return windowSize;
}

bool Window::IsCloseRequested() const {
	return glfwWindowShouldClose(_window);
}

bool Window::IsFocused() const {
	return glfwGetWindowAttrib(_window, GLFW_FOCUSED) == GLFW_TRUE;
}

bool Window::IsMaximized() const {
	return glfwGetWindowAttrib(_window, GLFW_MAXIMIZED) == GLFW_TRUE;
}

bool Window::IsMinimized() const {
	return glfwGetWindowAttrib(_window, GLFW_ICONIFIED) == GLFW_TRUE;
}

void Window::CenterPosition() {
	GLFWmonitor* monitor         = glfwGetPrimaryMonitor();
	const GLFWvidmode* videoMode = glfwGetVideoMode(monitor);

	int width, height;
	glfwGetWindowSize(_window, &width, &height);
	const int winX = (videoMode->width - width) / 2;
	const int winY = (videoMode->height - height) / 2;
	glfwSetWindowPos(_window, winX, winY);
}

void Window::Close() {
	glfwSetWindowShouldClose(_window, GLFW_TRUE);
}

Swapchain& Window::GetSwapchain() {
	return *_swapchain;
}

void Window::Hide() {
	glfwHideWindow(_window);
}

void Window::Maximize() {
	glfwMaximizeWindow(_window);
}

void Window::Minimize() {
	glfwIconifyWindow(_window);
}

void Window::SetPosition(const glm::ivec2& pos) {
	glfwSetWindowPos(_window, pos.x, pos.y);
}

void Window::SetSize(const glm::ivec2& size) {
	if (size.x <= 0 || size.y <= 0) { return; }

	glfwSetWindowSize(_window, size.x, size.y);
}

void Window::SetTitle(const std::string& title) {
	glfwSetWindowTitle(_window, title.c_str());
}

void Window::Restore() {
	glfwRestoreWindow(_window);
}

void Window::Show() {
	glfwShowWindow(_window);
}
}  // namespace Luna
