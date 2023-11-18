#include <Luna/Renderer/RenderGraph.hpp>
#include <Luna/Renderer/RenderPass.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/Semaphore.hpp>

namespace Luna {
RenderGraph::RenderGraph() {}

RenderGraph::~RenderGraph() noexcept {}

void RenderGraph::EnqueueRenderPasses(Vulkan::Device& device, TaskComposer& composer) {}

void RenderGraph::SetupAttachments(Vulkan::ImageView* backbuffer) {}

void RenderGraph::Bake() {
	// Clean up the previous baked information, if any.
	_passStack.clear();
	_passDependencies.clear();
	_passMergeDependencies.clear();
	_passDependencies.resize(_passes.size());
	_passMergeDependencies.resize(_passes.size());

	// Ensure our backbuffer exists and is written to.
	auto it = _resourceToIndex.find(_backbufferSource);
	if (it == _resourceToIndex.end()) { throw std::logic_error("[RenderGraph] Backbuffer source does not exist."); }
	auto& backbufferResource = *_resources[it->second];
	if (backbufferResource.GetWritePasses().empty()) {
		throw std::logic_error("[RenderGraph] Backbuffer source is never written to.");
	}

	// Allow the Render Passes a chance to set up their dependencies.
	for (auto& pass : _passes) { pass->SetupDependencies(); }

	// Ensure the Render Graph is sane.
	ValidatePasses();
}

void RenderGraph::Log() {}

void RenderGraph::Reset() {}

RenderPass& RenderGraph::AddPass(const std::string& name, RenderGraphQueueFlagBits queue) {
	auto it = _passToIndex.find(name);
	if (it != _passToIndex.end()) { return *_passes[it->second]; }

	uint32_t index = _passes.size();
	_passes.emplace_back(new RenderPass(*this, index, queue));
	_passes.back()->SetName(name);
	_passToIndex[name] = index;

	return *_passes.back();
}

RenderTextureResource& RenderGraph::GetTextureResource(const std::string& name) {
	auto it = _resourceToIndex.find(name);
	if (it != _resourceToIndex.end()) {
		Log::Assert(_resources[it->second]->GetType() == RenderResource::Type::Texture,
		            "RenderGraph",
		            "GetTextureResource() used to retrieve a non-texture resource.");

		return static_cast<RenderTextureResource&>(*_resources[it->second]);
	}

	uint32_t index = _resources.size();
	_resources.emplace_back(new RenderTextureResource(index));
	_resources.back()->SetName(name);
	_resourceToIndex[name] = index;

	return static_cast<RenderTextureResource&>(*_resources.back());
}

void RenderGraph::SetBackbufferDimensions(const ResourceDimensions& dimensions) {
	_backbufferDimensions = dimensions;
}

void RenderGraph::SetBackbufferSource(const std::string& source) {
	_backbufferSource = source;
}

std::vector<Vulkan::BufferHandle> RenderGraph::ConsumePhysicalBuffers() const {
	return _physicalBuffers;
}

void RenderGraph::InstallPhysicalBuffers(std::vector<Vulkan::BufferHandle>& buffers) {
	_physicalBuffers = std::move(buffers);
}

void RenderGraph::ValidatePasses() {
	for (auto& passPtr : _passes) {
		auto& pass = *passPtr;

		// Every blit output must have a matching blit input.
		if (pass.GetBlitTextureInputs().size() != pass.GetBlitTextureOutputs().size()) {}
	}
}
}  // namespace Luna
