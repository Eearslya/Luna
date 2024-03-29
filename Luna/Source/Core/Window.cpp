#include <GLFW/glfw3.h>

#include <Luna/Core/Input.hpp>
#include <Luna/Core/Window.hpp>
#include <Luna/Renderer/Swapchain.hpp>

namespace Luna {
Window::Window(const std::string& title, int width, int height, bool show) {
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
	glfwSetDropCallback(_window, [](GLFWwindow* window, int pathCount, const char** paths) {
		std::vector<std::filesystem::path> files;
		for (int i = 0; i < pathCount; ++i) { files.emplace_back(paths[i]); }
		Input::DropEvent(files);
	});
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
	_swapchain.Reset();
	glfwDestroyWindow(_window);
}

VkSurfaceKHR Window::CreateSurface(VkInstance instance) const {
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

Swapchain& Window::GetSwapchain() {
	return *_swapchain;
}

const Swapchain& Window::GetSwapchain() const {
	return *_swapchain;
}

Hash Window::GetSwapchainHash() const noexcept {
	return _swapchain->GetHash();
}

glm::ivec2 Window::GetWindowSize() const {
	glm::ivec2 size(0, 0);
	glfwGetWindowSize(_window, &size.x, &size.y);

	return size;
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

	const auto size = GetWindowSize();
	SetPosition((videoMode->width - size.x) / 2, (videoMode->height - size.y) / 2);
}

void Window::Close() {
	glfwSetWindowShouldClose(_window, GLFW_TRUE);
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

void Window::Restore() {
	glfwRestoreWindow(_window);
}

void Window::SetCursor(MouseCursor cursor) {
	if (_cursor != cursor) {
		glfwSetCursor(_window, WindowManager::GetCursor(cursor));
		_cursor = cursor;
	}
}

void Window::SetPosition(int x, int y) {
	glfwSetWindowPos(_window, x, y);
}

void Window::SetPosition(const glm::ivec2& pos) {
	SetPosition(pos.x, pos.y);
}

void Window::SetSize(int w, int h) {
	if (w <= 0 || h <= 0) { return; }

	glfwSetWindowSize(_window, w, h);
}

void Window::SetSize(const glm::ivec2& size) {
	SetSize(size.x, size.y);
}

void Window::SetTitle(const std::string& title) {
	glfwSetWindowTitle(_window, title.c_str());
}

void Window::Show() {
	glfwShowWindow(_window);
}
}  // namespace Luna
