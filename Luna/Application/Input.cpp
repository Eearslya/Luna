#include "Input.hpp"

#include <GLFW/glfw3.h>

namespace Luna {
bool Input::GetButton(MouseButton button) {
	return glfwGetMouseButton(_window, int(button)) == GLFW_PRESS;
}
}  // namespace Luna
