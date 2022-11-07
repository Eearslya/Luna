#include "Editor.hpp"

glm::uvec2 Editor::GetDefaultSize() const {
	return glm::uvec2(1600, 900);
}

std::string Editor::GetName() const {
	return "Luna Editor";
}

void Editor::Render() {}
