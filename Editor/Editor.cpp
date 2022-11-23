#include "Editor.hpp"

#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>

glm::uvec2 Editor::GetDefaultSize() const {
	return glm::uvec2(1600, 900);
}

std::string Editor::GetName() const {
	return "Luna Editor";
}

void Editor::Render() {
	auto& device = GetDevice();

	auto cmd = device.RequestCommandBuffer();
	device.Submit(cmd);
}
