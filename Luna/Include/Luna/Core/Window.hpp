#pragma once

#include <Luna/Core/Input.hpp>
#include <Luna/Utility/Delegate.hpp>
#include <Luna/Utility/IntrusivePtr.hpp>
#include <glm/glm.hpp>
#include <string>

struct GLFWwindow;
using VkInstance   = struct VkInstance_T*;
using VkSurfaceKHR = struct VkSurfaceKHR_T*;

namespace Luna {
class Swapchain;

class Window final {
 public:
	Window(const std::string& title, int width, int height, bool show = true);
	Window(const Window&)         = delete;
	void operator=(const Window&) = delete;
	~Window() noexcept;

	VkSurfaceKHR CreateSurface(VkInstance instance) const;
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
	Swapchain& GetSwapchain();
	void Hide();
	void Maximize();
	void Minimize();
	void Restore();
	void SetCursor(MouseCursor cursor);
	void SetPosition(const glm::ivec2& pos);
	void SetSize(const glm::ivec2& size);
	void SetTitle(const std::string& title);
	void Show();

	Delegate<void()> OnRefresh;

 private:
	GLFWwindow* _window = nullptr;
	IntrusivePtr<Swapchain> _swapchain;
	MouseCursor _cursor = MouseCursor::Arrow;
};
}  // namespace Luna
