#include <Luna/Application/Application.hpp>
#include <Luna/Application/Input.hpp>
#include <Luna/Vulkan/WSI.hpp>

namespace Luna {
InputAction Input::GetButton(MouseButton button) {
	return Application::Get()->GetWSI().GetButton(button);
}

bool Input::GetCursorHidden() {
	return _cursorHidden;
}

InputAction Input::GetKey(Key key) {
	return Application::Get()->GetWSI().GetKey(key);
}

void Input::SetCursorHidden(bool hidden) {}

void Input::SetMousePosition(const glm::dvec2& position) {}

void Input::CharEvent(int c) {
	OnChar(c);
}

void Input::DropEvent(const std::vector<std::filesystem::path>& paths) {
	OnFilesDropped(paths);
}

void Input::KeyEvent(Key key, InputAction action, InputMods mods) {
	OnKey(key, action, mods);
}

void Input::MouseButtonEvent(MouseButton button, InputAction action, InputMods mods) {
	OnMouseButton(button, action, mods);
}

void Input::MouseMovedEvent(const glm::dvec2& position) {
	if (_cursorHidden) {
		_position     = {_lastPosition.x - position.x, _lastPosition.y - position.y};
		_lastPosition = position;
	} else {
		_position = position;
	}

	OnMouseMoved(_position);
}

void Input::MouseScrolledEvent(const glm::dvec2& offset) {
	OnMouseScrolled(offset);
}
}  // namespace Luna
