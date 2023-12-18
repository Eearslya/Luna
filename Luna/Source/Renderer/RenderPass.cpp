#include <Luna/Renderer/RenderGraph.hpp>
#include <Luna/Renderer/RenderPass.hpp>

namespace Luna {
static constexpr RenderGraphQueueFlags ComputeQueues =
	RenderGraphQueueFlagBits::Compute | RenderGraphQueueFlagBits::AsyncCompute;

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

RenderTextureResource& RenderPass::AddHistoryInput(const std::string& name) {
	auto& res = _graph.GetTextureResource(name);
	res.AddQueue(_queue);
	res.AddImageUsage(vk::ImageUsageFlagBits::eSampled);
	_historyInputs.push_back(&res);

	return res;
}

RenderBufferResource& RenderPass::AddIndexBufferInput(const std::string& name) {
	return AddGenericBufferInput(name,
	                             vk::PipelineStageFlagBits2::eVertexInput,
	                             vk::AccessFlagBits2::eIndexRead,
	                             vk::BufferUsageFlagBits::eIndexBuffer);
}

RenderBufferResource& RenderPass::AddIndirectInput(const std::string& name) {
	return AddGenericBufferInput(name,
	                             vk::PipelineStageFlagBits2::eDrawIndirect,
	                             vk::AccessFlagBits2::eIndirectCommandRead,
	                             vk::BufferUsageFlagBits::eIndirectBuffer);
}

void RenderPass::AddProxyInput(const std::string& name, vk::PipelineStageFlags2 stages) {
	auto& res = _graph.GetProxyResource(name);
	res.AddQueue(_queue);
	res.ReadInPass(_index);

	AccessedProxyResource proxy;
	proxy.Proxy  = &res;
	proxy.Layout = vk::ImageLayout::eGeneral;
	proxy.Stages = stages;
	_proxyInputs.push_back(proxy);
}

RenderBufferResource& RenderPass::AddStorageInput(const std::string& name, vk::PipelineStageFlags2 stages) {
	if (!stages) {
		if ((_queue & ComputeQueues)) {
			stages = vk::PipelineStageFlagBits2::eComputeShader;
		} else {
			stages = vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader;
		}
	}

	return AddGenericBufferInput(
		name, stages, vk::AccessFlagBits2::eShaderStorageRead, vk::BufferUsageFlagBits::eStorageBuffer);
}

RenderTextureResource& RenderPass::AddTextureInput(const std::string& name, vk::PipelineStageFlags2 stages) {
	auto& res = _graph.GetTextureResource(name);
	res.AddQueue(_queue);
	res.ReadInPass(_index);
	res.AddImageUsage(vk::ImageUsageFlagBits::eSampled);

	auto it = std::find_if(_genericTextures.begin(), _genericTextures.end(), [&](const AccessedTextureResource& acc) {
		return acc.Texture == &res;
	});
	if (it != _genericTextures.end()) { return *it->Texture; }

	AccessedTextureResource acc = {};
	acc.Texture                 = &res;
	acc.Layout                  = vk::ImageLayout::eShaderReadOnlyOptimal;
	acc.Access                  = vk::AccessFlagBits2::eShaderSampledRead;
	if (stages) {
		acc.Stages = stages;
	} else if (_queue & ComputeQueues) {
		acc.Stages = vk::PipelineStageFlagBits2::eComputeShader;
	} else {
		acc.Stages = vk::PipelineStageFlagBits2::eFragmentShader;
	}

	_genericTextures.push_back(acc);

	return res;
}

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

void RenderPass::AddProxyOutput(const std::string& name, vk::PipelineStageFlags2 stages) {
	auto& res = _graph.GetProxyResource(name);
	res.AddQueue(_queue);
	res.WrittenInPass(_index);

	AccessedProxyResource proxy;
	proxy.Proxy  = &res;
	proxy.Layout = vk::ImageLayout::eGeneral;
	proxy.Stages = stages;
	_proxyInputs.push_back(proxy);
}

RenderTextureResource& RenderPass::AddStorageTextureOutput(const std::string& name,
                                                           const AttachmentInfo& info,
                                                           const std::string& input) {
	auto& res = _graph.GetTextureResource(name);
	res.AddQueue(_queue);
	res.WrittenInPass(_index);
	res.SetAttachmentInfo(info);
	res.AddImageUsage(vk::ImageUsageFlagBits::eStorage);
	_storageTextureOutputs.push_back(&res);

	if (!input.empty()) {
		auto& inputRes = _graph.GetTextureResource(input);
		inputRes.ReadInPass(_index);
		inputRes.AddImageUsage(vk::ImageUsageFlagBits::eStorage);
		_storageTextureInputs.push_back(&inputRes);
	} else {
		_storageTextureInputs.push_back(nullptr);
	}

	return res;
}

RenderBufferResource& RenderPass::AddStorageOutput(const std::string& name,
                                                   const BufferInfo& info,
                                                   const std::string& input) {
	auto& res = _graph.GetBufferResource(name);
	res.AddQueue(_queue);
	res.WrittenInPass(_index);
	res.SetBufferInfo(info);
	res.AddBufferUsage(vk::BufferUsageFlagBits::eStorageBuffer);
	_storageOutputs.push_back(&res);

	if (!input.empty()) {
		auto& inputRes = _graph.GetBufferResource(input);
		inputRes.ReadInPass(_index);
		inputRes.AddBufferUsage(vk::BufferUsageFlagBits::eStorageBuffer);
		_storageInputs.push_back(&inputRes);
	} else {
		_storageInputs.push_back(nullptr);
	}

	return res;
}

RenderTextureResource& RenderPass::SetDepthStencilOutput(const std::string& name, const AttachmentInfo& info) {
	auto& res = _graph.GetTextureResource(name);
	res.AddQueue(_queue);
	res.WrittenInPass(_index);
	res.SetAttachmentInfo(info);
	res.AddImageUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment);
	_depthStencilOutput = &res;

	return res;
}

void RenderPass::MakeColorInputScaled(uint32_t index) {
	std::swap(_colorScaleInputs[index], _colorInputs[index]);
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

RenderBufferResource& RenderPass::AddGenericBufferInput(const std::string& name,
                                                        vk::PipelineStageFlags2 stages,
                                                        vk::AccessFlags2 access,
                                                        vk::BufferUsageFlags usage) {
	auto& res = _graph.GetBufferResource(name);
	res.AddQueue(_queue);
	res.ReadInPass(_index);
	res.AddBufferUsage(usage);

	AccessedBufferResource acc = {};
	acc.Buffer                 = &res;
	acc.Layout                 = vk::ImageLayout::eGeneral;
	acc.Access                 = access;
	acc.Stages                 = stages;
	_genericBuffers.push_back(acc);

	return res;
}
}  // namespace Luna
