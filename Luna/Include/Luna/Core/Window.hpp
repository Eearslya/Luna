#pragma once

#include <Luna/Common.hpp>
#include <Luna/Core/WindowManager.hpp>

struct GLFWwindow;

namespace Luna {
class Window final {
 public:
	Window(const std::string& title, int width, int height, bool show = true);
	Window(const Window&)                     = delete;
	Window& operator=(const Window&) noexcept = delete;
	~Window() noexcept;

	glm::ivec2 GetFramebufferSize() const;
	GLFWwindow* GetHandle() const;
	glm::ivec2 GetPosition() const;
	glm::ivec2 GetWindowSize() const;
	bool IsCloseRequested() const;
	bool IsFocused() const;
	bool IsMaximized() const;
	bool IsMinimized() const;

	void CenterPosition();
	void Close();
	void Hide();
	void Maximize();
	void Minimize();
	void Restore();
	void SetCursor(MouseCursor cursor);
	void SetPosition(int x, int y);
	void SetPosition(const glm::ivec2& pos);
	void SetSize(int w, int h);
	void SetSize(const glm::ivec2& size);
	void SetTitle(const std::string& title);
	void Show();

 private:
	GLFWwindow* _window = nullptr;
	MouseCursor _cursor = MouseCursor::Arrow;
};
}  // namespace Luna
