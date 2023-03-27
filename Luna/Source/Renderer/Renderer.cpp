#include <Luna/Renderer/RenderContext.hpp>
#include <Luna/Renderer/RenderQueue.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Image.hpp>

namespace Luna {
Renderer::Renderer(Vulkan::Device& device, RendererType type) : _device(device), _type(type) {
	if (_type == RendererType::GeneralForward) {
		SetMeshRendererOptions(RendererOptionFlagBits::EnableShadows);
	} else {
		SetMeshRendererOptions({});
	}

	SetupShaderSuite();
}

void Renderer::Begin(RenderQueue& queue) const {
	queue.Reset();
	queue.SetShaderSuites(_shaderSuites.data());
}

void Renderer::Flush(Vulkan::CommandBuffer& cmd, RenderQueue& queue, const RenderContext& context) const {
	queue.Sort();
	FlushSubset(cmd, queue, context, 0, 1);
}

void Renderer::Flush(Vulkan::CommandBuffer& cmd, const RenderQueue& queue, const RenderContext& context) const {
	FlushSubset(cmd, queue, context, 0, 1);
}

void Renderer::FlushSubset(Vulkan::CommandBuffer& cmd,
                           const RenderQueue& queue,
                           const RenderContext& context,
                           uint32_t subsetIndex,
                           uint32_t subsetCount) const {
	// Bind global uniforms
	RenderParameters* params = cmd.AllocateTypedUniformData<RenderParameters>(0, 0, 1);
	*params                  = context.GetRenderParameters();

	cmd.SetOpaqueState();

	queue.DispatchSubset(RenderQueueType::Opaque, cmd, subsetIndex, subsetCount);
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
