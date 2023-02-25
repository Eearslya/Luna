#include <Luna/Renderer/RenderGraph.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>

namespace Luna {
bool RenderPassInterface::RenderPassIsConditional() const {
	return false;
}

bool RenderPassInterface::RenderPassIsSeparateLayered() const {
	return false;
}

bool RenderPassInterface::GetClearColor(uint32_t attachment, vk::ClearColorValue* value) const {
	if (value) { *value = {}; }

	return true;
}

bool RenderPassInterface::GetClearDepthStencil(vk::ClearDepthStencilValue* value) const {
	if (value) { *value = vk::ClearDepthStencilValue(1.0f, 0u); }

	return true;
}

bool RenderPassInterface::NeedRenderPass() const {
	return true;
}

RenderGraph::RenderGraph(Vulkan::Device& device) : _device(device) {}

RenderGraph::~RenderGraph() noexcept {}

void RenderGraph::Bake() {}

std::vector<Vulkan::BufferHandle> RenderGraph::ConsumePhysicalBuffers() const {
	return _physicalBuffers;
}

void RenderGraph::Reset() {
	_externalLockInterfaces.clear();
	_physicalAttachments.clear();
	_physicalBuffers.clear();
	_physicalDimensions.clear();
	_physicalEvents.clear();
	_physicalHistoryEvents.clear();
	_physicalHistoryImageAttachments.clear();
	_physicalImageAttachments.clear();
	_physicalPasses.clear();
	_renderPasses.clear();
	_renderPassToIndex.clear();
	_renderResources.clear();
	_renderResourceToIndex.clear();
}

void RenderGraph::SetBackbufferDimensions(const ResourceDimensions& dim) {
	_backbufferDimensions = dim;
}
}  // namespace Luna
