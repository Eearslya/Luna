#include "Input.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>

namespace Luna {
bool Input::GetButton(MouseButton button) {
	return glfwGetMouseButton(_window, int(button)) == GLFW_PRESS;
}

bool Input::GetCursorHidden() {
	return _cursorHidden;
}

bool Input::GetKey(Key key) {
	return glfwGetKey(_window, int(key)) == GLFW_PRESS;
}

void Input::SetCursorHidden(bool hidden) {
	if (_cursorHidden != hidden) {
		glfwSetInputMode(_window, GLFW_CURSOR, hidden ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

		if (hidden) {
			_savedPosition = _position;
			_position      = {0, 0};
			glfwGetCursorPos(_window, &_lastPosition.x, &_lastPosition.y);
		} else {
			SetMousePosition(_savedPosition);
		}

		if (ImGui::GetCurrentContext()) {
			ImGuiIO& io = ImGui::GetIO();
			if (hidden) {
				io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
			} else {
				io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
			}
		}
	}

	_cursorHidden = hidden;
}

void Input::SetMousePosition(const glm::dvec2& position) {
	_lastPosition = position;
	_position     = position;
	glfwSetCursorPos(_window, _position.x, _position.y);
}
}  // namespace Luna
