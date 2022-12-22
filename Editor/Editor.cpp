#include "Editor.hpp"

#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/RenderPass.hpp>

glm::uvec2 Editor::GetDefaultSize() const {
	return glm::uvec2(1600, 900);
}

std::string Editor::GetName() const {
	return "Luna Editor";
}

void Editor::Render() {
	auto& device = GetDevice();

	auto cmd    = device.RequestCommandBuffer();
	auto rpInfo = device.GetSwapchainRenderPass(Luna::Vulkan::SwapchainRenderPassType::Depth);
	cmd->BeginRenderPass(rpInfo);
	cmd->EndRenderPass();
	device.Submit(cmd);
}
