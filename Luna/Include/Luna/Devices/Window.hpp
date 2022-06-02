#pragma once

#include <Luna/Devices/Monitor.hpp>
#include <Luna/Utility/Delegate.hpp>
#include <Luna/Vulkan/Common.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace Luna {
class Window {
 public:
	Window();
	~Window() noexcept;

	static Window* Get() {
		return _instance;
	}

	void Update();

	GLFWwindow* GetWindow() const {
		return _window;
	}

	float GetAspectRatio() const {
		return static_cast<float>(_size.x) / static_cast<float>(_size.y);
	}
	const glm::uvec2& GetFramebufferSize() const {
		return _framebufferSize;
	}
	const std::vector<std::unique_ptr<Monitor>>& GetMonitors() const {
		return _monitors;
	}
	const glm::uvec2& GetPosition() const {
		return _position;
	}
	const glm::uvec2& GetSize(bool checkFullscreen = true) const {
		return (_fullscreen && checkFullscreen) ? _sizeFullscreen : _size;
	}
	const std::string& GetTitle() const {
		return _title;
	}
	bool IsBorderless() const {
		return _borderless;
	}
	bool IsFloating() const {
		return _floating;
	}
	bool IsFocused() const {
		return _focused;
	}
	bool IsFullscreen() const {
		return _fullscreen;
	}
	bool IsIconified() const {
		return _iconified;
	}
	bool IsResizable() const {
		return _resizable;
	}

	const Monitor* GetCurrentMonitor() const;
	const Monitor* GetPrimaryMonitor() const;
	std::vector<const char*> GetRequiredInstanceExtensions() const;

	VkSurfaceKHR CreateSurface(VkInstance instance) const;
	void Maximize();
	void SetBorderless(bool borderless);
	void SetFloating(bool floating);
	void SetFullscreen(bool fullscreen, const Monitor* monitor = nullptr);
	void SetIconified(bool iconified);
	void SetPosition(const glm::uvec2& position);
	void SetResizable(bool resizable);
	void SetSize(const glm::uvec2& size);
	void SetTitle(const std::string& title);

	Delegate<void(bool)> OnBorderlessChanged;
	Delegate<void()> OnClosed;
	Delegate<void(bool)> OnFocusChanged;
	Delegate<void(bool)> OnFloatingChanged;
	Delegate<void(bool)> OnFullscreenChanged;
	Delegate<void(bool)> OnIconifiedChanged;
	Delegate<void(Monitor*, bool)> OnMonitorChanged;
	Delegate<void(glm::uvec2)> OnMoved;
	Delegate<void(bool)> OnResizableChanged;
	Delegate<void(glm::uvec2)> OnResized;
	Delegate<void(std::string)> OnTitleChanged;

 private:
	static Window* _instance;
	static void CallbackError(int32_t error, const char* description);
	static void CallbackMonitor(GLFWmonitor* monitor, int32_t event);
	static void CallbackWindowClose(GLFWwindow* window);
	static void CallbackWindowFocus(GLFWwindow* window, int32_t focused);
	static void CallbackFramebufferSize(GLFWwindow* window, int32_t w, int32_t h);
	static void CallbackWindowIconify(GLFWwindow* window, int32_t iconified);
	static void CallbackWindowPosition(GLFWwindow* window, int32_t x, int32_t y);
	static void CallbackWindowSize(GLFWwindow* window, int32_t w, int32_t h);

	GLFWwindow* _window = nullptr;

	bool _borderless = false;
	bool _floating   = false;
	bool _focused    = true;
	bool _fullscreen = false;
	bool _iconified  = false;
	bool _resizable  = true;

	glm::uvec2 _position;
	glm::uvec2 _sizeFullscreen;
	glm::uvec2 _size;
	glm::uvec2 _framebufferSize;
	std::string _title;

	std::vector<std::unique_ptr<Monitor>> _monitors;

	bool _titleDirty = false;
};
}  // namespace Luna
