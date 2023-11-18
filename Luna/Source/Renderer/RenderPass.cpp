#include <Luna/Renderer/RenderGraph.hpp>
#include <Luna/Renderer/RenderPass.hpp>

namespace Luna {
bool RenderPassInterface::RenderPassIsConditional() const {
	return false;
}

bool RenderPassInterface::RenderPassIsSeparateLayered() const {
	return false;
}

bool RenderPassInterface::GetClearColor(uint32_t attachment, vk::ClearColorValue* value) const {
	return false;
}

bool RenderPassInterface::GetClearDepthStencil(vk::ClearDepthStencilValue* value) const {
	return false;
}

bool RenderPassInterface::NeedRenderPass() const {
	return true;
}

void RenderPassInterface::SetupDependencies(RenderPass& pass, RenderGraph& graph) {}

void RenderPassInterface::Setup() {}

void RenderPassInterface::BuildRenderPass(Vulkan::CommandBuffer& cmd) {}

void RenderPassInterface::BuildRenderPassSeparateLayer(Vulkan::CommandBuffer& cmd, uint32_t layer) {}

void RenderPassInterface::EnqueuePrepareRenderPass(RenderGraph& graph, TaskComposer& composer) {}

RenderPass::RenderPass(RenderGraph& graph, uint32_t index, RenderGraphQueueFlagBits queue)
		: _graph(graph), _index(index), _queue(queue) {}

RenderTextureResource& RenderPass::AddColorOutput(const std::string& name,
                                                  const AttachmentInfo& info,
                                                  const std::string& input) {
	auto& res = _graph.GetTextureResource(name);
	res.AddQueue(_queue);
	res.WrittenInPass(_index);
	res.SetAttachmentInfo(info);
	res.AddImageUsage(vk::ImageUsageFlagBits::eColorAttachment);
	if (info.MipLevels != 1) {
		res.AddImageUsage(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc);
	}
	_colorOutputs.push_back(&res);

	if (!input.empty()) {
		auto& inputRes = _graph.GetTextureResource(input);
		inputRes.ReadInPass(_index);
		inputRes.AddImageUsage(vk::ImageUsageFlagBits::eColorAttachment);
		_colorInputs.push_back(&inputRes);
		_colorScaleInputs.push_back(nullptr);
	} else {
		_colorInputs.push_back(nullptr);
		_colorScaleInputs.push_back(nullptr);
	}

	return res;
}

bool RenderPass::GetClearColor(uint32_t index, vk::ClearColorValue* value) const {
	if (_interface) {
		return _interface->GetClearColor(index, value);
	} else if (_getClearColorFn) {
		return _getClearColorFn(index, value);
	}

	return false;
}

bool RenderPass::GetClearDepthStencil(vk::ClearDepthStencilValue* value) const {
	if (_interface) {
		return _interface->GetClearDepthStencil(value);
	} else if (_getClearDepthStencilFn) {
		return _getClearDepthStencilFn(value);
	}

	return false;
}

bool RenderPass::NeedRenderPass() const {
	if (_interface) { return _interface->NeedRenderPass(); }

	return true;
}

bool RenderPass::RenderPassIsConditional() const {
	if (_interface) { return _interface->RenderPassIsConditional(); }

	return false;
}

bool RenderPass::RenderPassIsMultiview() const {
	if (_interface) { return !_interface->RenderPassIsSeparateLayered(); }

	return true;
}

void RenderPass::BuildRenderPass(Vulkan::CommandBuffer& cmd, uint32_t layer) {
	if (_interface) {
		if (_interface->RenderPassIsSeparateLayered()) {
			_interface->BuildRenderPassSeparateLayer(cmd, layer);
		} else {
			_interface->BuildRenderPass(cmd);
		}
	} else if (_buildRenderPassFn) {
		_buildRenderPassFn(cmd);
	}
}

void RenderPass::PrepareRenderPass(TaskComposer& composer) {
	if (_interface) { _interface->EnqueuePrepareRenderPass(_graph, composer); }
}

void RenderPass::Setup() {
	if (_interface) { _interface->Setup(); }
}

void RenderPass::SetupDependencies() {
	if (_interface) { _interface->SetupDependencies(*this, _graph); }
}

void RenderPass::SetBuildRenderPass(std::function<void(Vulkan::CommandBuffer&)>&& func) noexcept {
	_buildRenderPassFn = std::move(func);
}

void RenderPass::SetGetClearColor(std::function<bool(uint32_t, vk::ClearColorValue*)>&& func) noexcept {
	_getClearColorFn = std::move(func);
}

void RenderPass::SetGetClearDepthStencil(std::function<bool(vk::ClearDepthStencilValue*)>&& func) noexcept {
	_getClearDepthStencilFn = std::move(func);
}

void RenderPass::SetRenderPassInterface(RenderPassInterfaceHandle interface) noexcept {
	_interface = std::move(interface);
}

void RenderPass::SetName(const std::string& name) noexcept {
	_name = name;
}

void RenderPass::SetPhysicalPassIndex(uint32_t index) noexcept {
	_physicalPass = index;
}
}  // namespace Luna
