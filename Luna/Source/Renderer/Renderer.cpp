#include <Luna/Renderer/RenderContext.hpp>
#include <Luna/Renderer/RenderQueue.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Image.hpp>

namespace Luna {
Renderer::Renderer(Vulkan::Device& device, RendererType type) : _device(device), _type(type) {
	SetupShaderSuite();
	UpdateShaderDefines();

	if (_type == RendererType::GeneralForward) {
		SetMeshRendererOptions(RendererOptionFlagBits::EnableShadows);
	} else {
		SetMeshRendererOptions({});
	}
}

void Renderer::Begin(RenderQueue& queue) const {
	queue.Reset();
	queue.SetShaderSuites(_shaderSuites.data());
}

void Renderer::Flush(Vulkan::CommandBuffer& cmd,
                     RenderQueue& queue,
                     const RenderContext& context,
                     RendererFlushFlags flush) const {
	queue.Sort();
	FlushSubset(cmd, queue, context, 0, 1, flush);
}

void Renderer::Flush(Vulkan::CommandBuffer& cmd,
                     const RenderQueue& queue,
                     const RenderContext& context,
                     RendererFlushFlags flush) const {
	FlushSubset(cmd, queue, context, 0, 1, flush);
}

void Renderer::FlushSubset(Vulkan::CommandBuffer& cmd,
                           const RenderQueue& queue,
                           const RenderContext& context,
                           uint32_t subsetIndex,
                           uint32_t subsetCount,
                           RendererFlushFlags flush) const {
	// Bind global uniforms
	RenderParameters* params = cmd.AllocateTypedUniformData<RenderParameters>(0, 0, 1);
	*params                  = context.GetRenderParameters();

	// Bind texture array
	cmd.SetBindless(1, context.GetBindlessSet());

	cmd.SetOpaqueState();

	if (flush & RendererFlushFlagBits::FrontFaceClockwise) {}
	if (flush & RendererFlushFlagBits::NoColor) {}
	if (flush & RendererFlushFlagBits::DepthStencilReadOnly) { cmd.SetDepthWrite(false); }
	if (flush & RendererFlushFlagBits::Backface) {}
	if (flush & RendererFlushFlagBits::DepthTestEqual) {
		cmd.SetDepthCompareOp(vk::CompareOp::eEqual);
	} else if (flush & RendererFlushFlagBits::DepthTestInvert) {
	}

	queue.DispatchSubset(RenderQueueType::Opaque, cmd, subsetIndex, subsetCount);
	queue.DispatchSubset(RenderQueueType::OpaqueEmissive, cmd, subsetIndex, subsetCount);
}

void Renderer::SetMeshRendererOptions(RendererOptionFlags options) {
	if (options == _options) { return; }
	_options = options;
	UpdateShaderDefines();
}

std::vector<std::pair<std::string, int>> Renderer::BuildShaderDefines() {
	std::vector<std::pair<std::string, int>> defines;
	if (_options & RendererOptionFlagBits::EnableShadows) { defines.emplace_back("SHADOWS", 1); }

	switch (_type) {
		case RendererType::GeneralForward:
			defines.emplace_back("RENDERER_FORWARD", 1);
			break;

		case RendererType::DepthOnly:
			defines.emplace_back("RENDERER_DEPTH", 1);
			break;

		default:
			break;
	}

	return defines;
}

void Renderer::SetupShaderSuite() {
	ShaderSuiteResolver defaultResolver;
	for (uint32_t i = 0; i < RenderableTypeCount; ++i) {
		defaultResolver.Resolve(_device, _shaderSuites[i], _type, static_cast<RenderableType>(i));
	}
}

void Renderer::UpdateShaderDefines() {
	auto globalDefines = BuildShaderDefines();

	auto& mesh            = _shaderSuites[int(RenderableType::Mesh)];
	mesh.GetBaseDefines() = globalDefines;
	mesh.BakeBaseDefines();
}
}  // namespace Luna
