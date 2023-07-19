#include <GLFW/glfw3.h>

#include <Luna/Core/Window.hpp>

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

	if (show) {
		glfwShowWindow(_window);
		glfwFocusWindow(_window);
	}
}

Window::~Window() noexcept {
	glfwDestroyWindow(_window);
}

bool Window::IsCloseRequested() const {
	return glfwWindowShouldClose(_window);
}

void Window::Show() {
	glfwShowWindow(_window);
}
}  // namespace Luna
