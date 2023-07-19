#pragma once

#include <Luna/Common.hpp>

struct GLFWwindow;

namespace Luna {
class Window final {
 public:
	Window(const std::string& title, int width, int height, bool show = true);
	Window(const Window&)                     = delete;
	Window& operator=(const Window&) noexcept = delete;
	~Window() noexcept;

	bool IsCloseRequested() const;

	void Show();

 private:
	GLFWwindow* _window = nullptr;
};
}  // namespace Luna
