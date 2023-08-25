#pragma once

#include <Luna/Common.hpp>
#include <Luna/Core/WindowManager.hpp>

struct GLFWwindow;
using VkInstance   = struct VkInstance_T*;
using VkSurfaceKHR = struct VkSurfaceKHR_T*;

namespace Luna {
class Swapchain;

class Window final {
 public:
	Window(const std::string& title, int width, int height, bool show = true);
	Window(const Window&)                     = delete;
	Window& operator=(const Window&) noexcept = delete;
	~Window() noexcept;

	[[nodiscard]] VkSurfaceKHR CreateSurface(VkInstance instance) const;
	[[nodiscard]] glm::ivec2 GetFramebufferSize() const;
	[[nodiscard]] GLFWwindow* GetHandle() const;
	[[nodiscard]] glm::ivec2 GetPosition() const;
	[[nodiscard]] Swapchain& GetSwapchain();
	[[nodiscard]] const Swapchain& GetSwapchain() const;
	[[nodiscard]] glm::ivec2 GetWindowSize() const;
	[[nodiscard]] bool IsCloseRequested() const;
	[[nodiscard]] bool IsFocused() const;
	[[nodiscard]] bool IsMaximized() const;
	[[nodiscard]] bool IsMinimized() const;

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
	IntrusivePtr<Swapchain> _swapchain;
};
}  // namespace Luna
