#include <GLFW/glfw3.h>

#include <Luna/Devices/Keyboard.hpp>

namespace Luna {
void Keyboard::CallbackKey(GLFWwindow* window, int32_t key, int32_t scancode, int32_t action, int32_t mods) {
	Keyboard::Get()->_onKey(static_cast<Key>(key), static_cast<InputAction>(action), MakeBitmask<InputModBits>(mods));
}

void Keyboard::CallbackChar(GLFWwindow* window, uint32_t codepoint) {
	Keyboard::Get()->_onChar(static_cast<char>(codepoint));
}

Keyboard::Keyboard() {
	auto glfwWindow = Window::Get()->GetWindow();
	glfwSetKeyCallback(glfwWindow, CallbackKey);
	glfwSetCharCallback(glfwWindow, CallbackChar);
}

void Keyboard::Update() {}

InputAction Keyboard::GetKey(Key key) const {
	const auto state = glfwGetKey(Window::Get()->GetWindow(), static_cast<int32_t>(key));

	return static_cast<InputAction>(state);
}
}  // namespace Luna
