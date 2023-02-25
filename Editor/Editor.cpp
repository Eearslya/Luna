#include "Editor.hpp"

#include <Luna/Utility/Threading.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/RenderPass.hpp>

glm::uvec2 Editor::GetDefaultSize() const {
	return glm::uvec2(1600, 900);
}

std::string Editor::GetName() const {
	return "Luna Editor";
}

void Editor::Start() {
	_renderGraph = std::make_unique<Luna::RenderGraph>(GetDevice());
}

void Editor::Render() {
	Luna::TaskComposer composer;

	if (_swapchainDirty) {
		BakeRenderGraph();
		_swapchainDirty = false;
	}

	auto& device = GetDevice();

	auto cmd    = device.RequestCommandBuffer();
	auto rpInfo = device.GetSwapchainRenderPass(Luna::Vulkan::SwapchainRenderPassType::Depth);
	cmd->BeginRenderPass(rpInfo);
	cmd->EndRenderPass();
	device.Submit(cmd);
}

void Editor::Stop() {
	_renderGraph.reset();
}

void Editor::OnSwapchainChanged(const Luna::Vulkan::SwapchainConfiguration& config) {
	_swapchainConfig = config;
	_swapchainDirty  = true;
}

void Editor::BakeRenderGraph() {
	auto physicalBuffers = _renderGraph->ConsumePhysicalBuffers();

	_renderGraph->Reset();
	GetDevice().NextFrame();

	const Luna::ResourceDimensions backbufferDims = {.Format    = _swapchainConfig.Format.format,
	                                                 .Width     = _swapchainConfig.Extent.width,
	                                                 .Height    = _swapchainConfig.Extent.height,
	                                                 .Transform = _swapchainConfig.Transform};
	_renderGraph->SetBackbufferDimensions(backbufferDims);

	_renderGraph->Bake();
}
