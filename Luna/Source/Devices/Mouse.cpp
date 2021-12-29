#include <GLFW/glfw3.h>

#include <Luna/Core/Engine.hpp>
#include <Luna/Devices/Mouse.hpp>

namespace Luna {
void Mouse::CallbackButton(GLFWwindow* window, int32_t button, int32_t action, int32_t mods) {
	Mouse::Get()->_onButton(
		static_cast<MouseButton>(button), static_cast<InputAction>(action), MakeBitmask<InputModBits>(mods));
}

void Mouse::CallbackPosition(GLFWwindow* window, double x, double y) {
	auto mouse = Mouse::Get();
	if (mouse->_cursorHidden) {
		mouse->_position     = {mouse->_lastPosition.x - x, mouse->_lastPosition.y - y};
		mouse->_lastPosition = {x, y};
	} else {
		mouse->_position = {x, y};
	}
	mouse->_onMoved(mouse->_position);
}

void Mouse::CallbackEnter(GLFWwindow* window, int32_t entered) {
	auto mouse             = Mouse::Get();
	mouse->_windowSelected = entered == GLFW_TRUE;
	mouse->_onEnter(mouse->_windowSelected);
}

void Mouse::CallbackScroll(GLFWwindow* window, double xOffset, double yOffset) {
	auto mouse     = Mouse::Get();
	mouse->_scroll = {xOffset, yOffset};
	mouse->_onScroll(mouse->_scroll);
}

Mouse::Mouse() {
	auto glfwWindow = Window::Get()->GetWindow();
	glfwSetMouseButtonCallback(glfwWindow, CallbackButton);
	glfwSetCursorPosCallback(glfwWindow, CallbackPosition);
	glfwSetCursorEnterCallback(glfwWindow, CallbackEnter);
	glfwSetScrollCallback(glfwWindow, CallbackScroll);
}

Mouse::~Mouse() noexcept {}

void Mouse::Update() {
	auto delta = Engine::Get()->GetUpdateDelta().Seconds();

	_positionDelta = delta * (_lastPosition - _position);
	if (!_cursorHidden) { _lastPosition = _position; }
	_scrollDelta = delta * (_lastScroll - _scroll);
	_lastScroll  = _scroll;
}

InputAction Mouse::GetButton(MouseButton button) const {
	const auto state = glfwGetMouseButton(Window::Get()->GetWindow(), static_cast<int32_t>(button));

	return static_cast<InputAction>(state);
}

void Mouse::SetCursorHidden(bool hidden) {
	if (_cursorHidden != hidden) {
		glfwSetInputMode(Window::Get()->GetWindow(), GLFW_CURSOR, hidden ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

		if (hidden) {
			_savedPosition = _position;
			_position      = {0, 0};
			glfwGetCursorPos(Window::Get()->GetWindow(), &_lastPosition.x, &_lastPosition.y);
		} else {
			SetPosition(_savedPosition);
		}
	}

	_cursorHidden = hidden;
}

void Mouse::SetPosition(const Vec2d& position) {
	_lastPosition = position;
	_position     = position;
	glfwSetCursorPos(Window::Get()->GetWindow(), _position.x, _position.y);
}
}  // namespace Luna
