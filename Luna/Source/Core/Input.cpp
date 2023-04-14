#include <GLFW/glfw3.h>

#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Input.hpp>
#include <Luna/Core/Window.hpp>

namespace Luna {
static struct InputState {
	bool CursorHidden       = false;
	glm::dvec2 LastPosition = {0, 0};
	glm::dvec2 Position     = {0, 0};
} State;

InputAction Input::GetButton(MouseButton button) {
	return static_cast<InputAction>(glfwGetMouseButton(Engine::GetMainWindow()->GetHandle(), static_cast<int>(button)));
}

bool Input::GetCursorHidden() {
	return State.CursorHidden;
}

glm::dvec2 Input::GetCursorPosition() {
	return State.Position;
}

InputAction Input::GetKey(Key key) {
	return static_cast<InputAction>(glfwGetKey(Engine::GetMainWindow()->GetHandle(), static_cast<int>(key)));
}

void Input::SetCursorHidden(bool hidden) {
	if (State.CursorHidden != hidden) {
		State.CursorHidden = hidden;
		glfwSetInputMode(Engine::GetMainWindow()->GetHandle(),
		                 GLFW_CURSOR,
		                 State.CursorHidden ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
		if (State.CursorHidden) {
			glfwGetCursorPos(Engine::GetMainWindow()->GetHandle(), &State.LastPosition.x, &State.LastPosition.y);
			State.Position = glm::dvec2(0);
		}
	}
}

void Input::SetCursorPosition(const glm::dvec2& position) {
	glfwSetCursorPos(Engine::GetMainWindow()->GetHandle(), position.x, position.y);
	glfwGetCursorPos(Engine::GetMainWindow()->GetHandle(), &State.Position.x, &State.Position.y);
}

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

void Input::MouseMovedEvent(const glm::dvec2& pos) {
	if (State.CursorHidden) {
		State.Position     = {State.LastPosition - pos};
		State.LastPosition = pos;
	} else {
		State.Position = pos;
	}

	OnMouseMoved(State.Position);
}

void Input::MouseScrolledEvent(const glm::dvec2& delta) {
	OnMouseScrolled(delta);
}
}  // namespace Luna
