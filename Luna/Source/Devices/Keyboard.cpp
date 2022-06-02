#include <GLFW/glfw3.h>
#include <imgui.h>

#include <Luna/Devices/Keyboard.hpp>
#include <Luna/Devices/Window.hpp>
#include <Tracy.hpp>

namespace Luna {
Keyboard* Keyboard::_instance = nullptr;

void Keyboard::CallbackKey(GLFWwindow* window, int32_t key, int32_t scancode, int32_t action, int32_t mods) {
	bool uiCapture = false;
	if (ImGui::GetCurrentContext()) {
		ImGuiIO& io = ImGui::GetIO();
		uiCapture   = io.WantCaptureKeyboard;
	}
	Keyboard::Get()->_onKey(
		static_cast<Key>(key), static_cast<InputAction>(action), MakeBitmask<InputModBits>(mods), uiCapture);
}

void Keyboard::CallbackChar(GLFWwindow* window, uint32_t codepoint) {
	Keyboard::Get()->_onChar(static_cast<char>(codepoint));
}

Keyboard::Keyboard() {
	if (_instance) { throw std::runtime_error("Keyboard was initialized more than once!"); }
	_instance = this;

	ZoneScopedN("Keyboard::Keyboard()");

	auto glfwWindow = Window::Get()->GetWindow();
	glfwSetKeyCallback(glfwWindow, CallbackKey);
	glfwSetCharCallback(glfwWindow, CallbackChar);
}

InputAction Keyboard::GetKey(Key key, bool allowGuiOverride) const {
	if (allowGuiOverride && ImGui::GetCurrentContext()) {
		ImGuiIO& io = ImGui::GetIO();
		if (io.WantCaptureKeyboard) { return InputAction::Release; }
	}

	const auto state = glfwGetKey(Window::Get()->GetWindow(), static_cast<int32_t>(key));

	return static_cast<InputAction>(state);
}
}  // namespace Luna
