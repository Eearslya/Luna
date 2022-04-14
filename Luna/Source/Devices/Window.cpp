#include <GLFW/glfw3.h>

#include <Luna/Core/Log.hpp>
#include <Luna/Devices/Window.hpp>
#include <Tracy.hpp>

namespace Luna {
void Window::CallbackError(int32_t error, const char* description) {
	Log::Error("GLFW Error {}: {}", error, description);
}

void Window::CallbackMonitor(GLFWmonitor* monitor, int32_t event) {
	auto window    = Window::Get();
	auto& monitors = window->_monitors;

	if (event == GLFW_CONNECTED) {
		auto& it = monitors.emplace_back(std::make_unique<Monitor>(monitor));
		window->_onMonitorChanged(it.get(), true);
	} else {
		for (auto& m : monitors) {
			if (m->GetMonitor() == monitor) { window->_onMonitorChanged(m.get(), false); }
		}

		monitors.erase(std::remove_if(
			monitors.begin(), monitors.end(), [monitor](const auto& m) { return monitor == m->GetMonitor(); }));
	}
}

void Window::CallbackWindowClose(GLFWwindow* window) {
	Window::Get()->_onClosed();
	Engine::Get()->Shutdown();
}

void Window::CallbackWindowFocus(GLFWwindow* window, int32_t focused) {
	auto me      = Window::Get();
	me->_focused = focused == GLFW_TRUE;
	me->_onFocusChanged(me->_focused);
}

void Window::CallbackFramebufferSize(GLFWwindow* window, int32_t w, int32_t h) {
	auto me = Window::Get();
	if (me->_fullscreen) {
		me->_sizeFullscreen = {w, h};
	} else {
		me->_size = {w, h};
	}
}

void Window::CallbackWindowIconify(GLFWwindow* window, int32_t iconified) {
	auto me        = Window::Get();
	me->_iconified = iconified == GLFW_TRUE;
	me->_onIconifiedChanged(me->_iconified);
}

void Window::CallbackWindowPosition(GLFWwindow* window, int32_t x, int32_t y) {
	auto me = Window::Get();
	if (me->_fullscreen) { return; }
	me->_position = {x, y};
	me->_onMoved(me->_position);
}

void Window::CallbackWindowSize(GLFWwindow* window, int32_t w, int32_t h) {
	if (w <= 0 || h <= 0) { return; }

	auto me = Window::Get();
	if (me->_fullscreen) {
		me->_sizeFullscreen = {w, h};
		me->_onResized(me->_sizeFullscreen);
	} else {
		me->_size = {w, h};
		me->_onResized(me->_size);
	}
}

Window::Window() : _size(1280, 720), _title("Luna") {
	ZoneScopedN("Window::Window()");

	glfwSetErrorCallback(CallbackError);

	if (glfwInit() == GLFW_FALSE) { throw std::runtime_error("Failed to initialize GLFW!"); }

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_STENCIL_BITS, 8);
	glfwWindowHint(GLFW_STEREO, GLFW_FALSE);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

	glfwSetMonitorCallback(CallbackMonitor);

	int32_t monitorCount = 0;
	auto monitors        = glfwGetMonitors(&monitorCount);
	for (uint32_t i = 0; i < monitorCount; ++i) { _monitors.emplace_back(std::make_unique<Monitor>(monitors[i])); }

	const auto monitor   = GetPrimaryMonitor();
	const auto videoMode = monitor->GetVideoMode();

	_window = glfwCreateWindow(_size.x, _size.y, _title.c_str(), nullptr, nullptr);
	if (_window == nullptr) {
		glfwTerminate();
		throw std::runtime_error("Failed to create window!");
	}

	glfwSetWindowUserPointer(_window, this);
	glfwSetWindowAttrib(_window, GLFW_DECORATED, !_borderless);
	glfwSetWindowAttrib(_window, GLFW_FLOATING, _floating);
	glfwSetWindowAttrib(_window, GLFW_RESIZABLE, _resizable);

	_position = {(videoMode.Width - _size.x) / 2, (videoMode.Height - _size.y) / 2};
	glfwSetWindowPos(_window, _position.x, _position.y);

	if (_fullscreen) { SetFullscreen(true, monitor); }

	glfwShowWindow(_window);

	glfwSetFramebufferSizeCallback(_window, CallbackFramebufferSize);
	glfwSetWindowCloseCallback(_window, CallbackWindowClose);
	glfwSetWindowFocusCallback(_window, CallbackWindowFocus);
	glfwSetWindowIconifyCallback(_window, CallbackWindowIconify);
	glfwSetWindowPosCallback(_window, CallbackWindowPosition);
	glfwSetWindowSizeCallback(_window, CallbackWindowSize);
}

Window::~Window() noexcept {
	glfwDestroyWindow(_window);
	glfwTerminate();
}

void Window::Update() {
	ZoneScopedN("Window::Update()");

	glfwPollEvents();

	if (_titleDirty) {
		glfwSetWindowTitle(_window, _title.c_str());
		_titleDirty = false;
	}
}

const Monitor* Window::GetCurrentMonitor() const {
	const auto OverlappingArea =
		[](const glm::ivec2& l1, const glm::ivec2& r1, const glm::ivec2& l2, const glm::ivec2& r2) -> int32_t {
		const int area1 = std::abs(l1.x - r1.x) * std::abs(l1.y - r1.y);
		const int area2 = std::abs(l2.x - r2.x) * std::abs(l2.y - r2.y);
		const int areaI = (std::min(r1.x, r2.x) - std::max(l1.x, l2.x)) * (std::min(r1.y, r2.y) - std::max(l1.y, l2.y));
		return area1 + area2 - areaI;
	};

	if (_fullscreen) {
		const auto glfwMonitor = glfwGetWindowMonitor(_window);
		for (const auto& monitor : _monitors) {
			if (monitor->GetMonitor() == glfwMonitor) { return monitor.get(); }
		}
		return nullptr;
	}

	std::multimap<int32_t, const Monitor*> ranking;
	for (const auto& monitor : _monitors) {
		const auto workArea = monitor->GetWorkareaSize();
		const auto workPos  = monitor->GetWorkareaPosition();
		ranking.emplace(OverlappingArea(workPos, workPos + workArea, _position, _position + _size), monitor.get());
	}
	if (ranking.begin()->first > 0) { return ranking.begin()->second; }

	return nullptr;
}

const Monitor* Window::GetPrimaryMonitor() const {
	for (const auto& monitor : _monitors) {
		if (monitor->IsPrimary()) { return monitor.get(); }
	}

	return nullptr;
}

std::vector<const char*> Window::GetRequiredInstanceExtensions() const {
	uint32_t extensionCount = 0;
	const auto extensions   = glfwGetRequiredInstanceExtensions(&extensionCount);
	return std::vector<const char*>(extensions, extensions + extensionCount);
}

VkSurfaceKHR Window::CreateSurface(VkInstance instance) const {
	VkSurfaceKHR surface  = VK_NULL_HANDLE;
	const VkResult result = glfwCreateWindowSurface(instance, _window, nullptr, &surface);
	if (result == VK_SUCCESS) {
		return surface;
	} else {
		throw std::runtime_error("Failed to create window surface!");
	}
}

void Window::Maximize() {
	glfwMaximizeWindow(_window);
}

void Window::SetBorderless(bool borderless) {
	if (borderless == _borderless) { return; }
	_borderless = borderless;
	glfwSetWindowAttrib(_window, GLFW_DECORATED, !_borderless);
	_onBorderlessChanged(_borderless);
}

void Window::SetFloating(bool floating) {
	if (floating == _floating) { return; }
	_floating = floating;
	glfwSetWindowAttrib(_window, GLFW_FLOATING, !_floating);
	_onFloatingChanged(_floating);
}

void Window::SetFullscreen(bool fullscreen, const Monitor* monitor) {
	if (fullscreen == _fullscreen && monitor == GetCurrentMonitor()) { return; }

	const auto selected  = monitor ? monitor : GetCurrentMonitor();
	const auto videoMode = selected->GetVideoMode();
	_fullscreen          = fullscreen;

	if (_fullscreen) {
		_sizeFullscreen = {videoMode.Width, videoMode.Height};
		glfwSetWindowMonitor(_window, selected->GetMonitor(), 0, 0, _sizeFullscreen.x, _sizeFullscreen.y, GLFW_DONT_CARE);
	} else {
		_position =
			glm::uvec2((glm::ivec2(videoMode.Width, videoMode.Height) - glm::ivec2(_size)) / 2) + selected->GetPosition();
		glfwSetWindowMonitor(_window, nullptr, _position.x, _position.y, _size.x, _size.y, GLFW_DONT_CARE);
	}

	_onFullscreenChanged(_fullscreen);
}

void Window::SetIconified(bool iconified) {
	if (iconified == _iconified) { return; }
	_iconified = iconified;
	if (_iconified) {
		glfwIconifyWindow(_window);
	} else {
		glfwRestoreWindow(_window);
	}
}

void Window::SetPosition(const glm::uvec2& position) {
	if (position.x >= 0) { _position.x = position.x; }
	if (position.y >= 0) { _position.y = position.y; }
	glfwSetWindowPos(_window, _position.x, _position.y);
}

void Window::SetResizable(bool resizable) {
	if (resizable == _resizable) { return; }
	_resizable = resizable;
	glfwSetWindowAttrib(_window, GLFW_RESIZABLE, !_resizable);
	_onResizableChanged(_resizable);
}

void Window::SetSize(const glm::uvec2& size) {
	if (size.x >= 0) { _size.x = size.x; }
	if (size.y >= 0) { _size.y = size.y; }
	glfwSetWindowSize(_window, _size.x, _size.y);
}

void Window::SetTitle(const std::string& title) {
	_title      = title;
	_titleDirty = true;
	_onTitleChanged(_title);
}
}  // namespace Luna
