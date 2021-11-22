#pragma once

#include <Luna/Core/Engine.hpp>
#include <Luna/Devices/Monitor.hpp>
#include <Luna/Math/Vec2.hpp>
#include <Luna/Utility/Delegate.hpp>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace Luna {
class Window : public Module::Registrar<Window> {
	static inline const bool Registered = Register("Window", Stage::Pre);

 public:
	Window();
	~Window() noexcept;

	virtual void Update() override;

	GLFWwindow* GetWindow() const {
		return _window;
	}

	float GetAspectRatio() const {
		return static_cast<float>(_size.x) / static_cast<float>(_size.y);
	}
	const std::vector<std::unique_ptr<Monitor>>& GetMonitors() const {
		return _monitors;
	}
	const Vec2ui& GetPosition() const {
		return _position;
	}
	const Vec2ui& GetSize(bool checkFullscreen = true) const {
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

	void SetBorderless(bool borderless);
	void SetFloating(bool floating);
	void SetFullscreen(bool fullscreen, const Monitor* monitor = nullptr);
	void SetIconified(bool iconified);
	void SetPosition(const Vec2ui& position);
	void SetResizable(bool resizable);
	void SetSize(const Vec2ui& size);
	void SetTitle(const std::string& title);

	Delegate<void(bool)>& OnBorderlessChanged() {
		return _onBorderlessChanged;
	}
	Delegate<void()>& OnClosed() {
		return _onClosed;
	}
	Delegate<void(bool)>& OnFocusChanged() {
		return _onFocusChanged;
	}
	Delegate<void(bool)>& OnFloatingChanged() {
		return _onFloatingChanged;
	}
	Delegate<void(bool)>& OnFullscreenChanged() {
		return _onFullscreenChanged;
	}
	Delegate<void(bool)>& OnIconifiedChanged() {
		return _onIconifiedChanged;
	}
	Delegate<void(Monitor*, bool)>& OnMonitorChanged() {
		return _onMonitorChanged;
	}
	Delegate<void(Vec2ui)>& OnMoved() {
		return _onMoved;
	}
	Delegate<void(bool)>& OnResizableChanged() {
		return _onResizableChanged;
	}
	Delegate<void(Vec2ui)>& OnResized() {
		return _onResized;
	}
	Delegate<void(std::string)>& OnTitleChanged() {
		return _onTitleChanged;
	}

 private:
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

	Vec2ui _position;
	Vec2ui _sizeFullscreen;
	Vec2ui _size;
	std::string _title;

	std::vector<std::unique_ptr<Monitor>> _monitors;

	Delegate<void(bool)> _onBorderlessChanged;
	Delegate<void()> _onClosed;
	Delegate<void(bool)> _onFocusChanged;
	Delegate<void(bool)> _onFloatingChanged;
	Delegate<void(bool)> _onFullscreenChanged;
	Delegate<void(bool)> _onIconifiedChanged;
	Delegate<void(Monitor*, bool)> _onMonitorChanged;
	Delegate<void(Vec2ui)> _onMoved;
	Delegate<void(bool)> _onResizableChanged;
	Delegate<void(Vec2ui)> _onResized;
	Delegate<void(std::string)> _onTitleChanged;
};
}  // namespace Luna
