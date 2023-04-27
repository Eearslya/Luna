#include <Luna/Renderer/RenderContext.hpp>
#include <Luna/Renderer/RenderQueue.hpp>
#include <Luna/Renderer/RenderRunner.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>

namespace Luna {
RenderRunner::RenderRunner(RendererType type) : _type(type) {
	SetupShaderSuite();
	UpdateShaderDefines();

	if (_type == RendererType::GeneralForward) {
		SetMeshRendererOptions(RendererOptionFlagBits::EnableShadows);
	} else {
		SetMeshRendererOptions({});
	}
}

void RenderRunner::Begin(RenderQueue& queue) const {
	queue.Reset();
	queue.SetShaderSuites(_shaderSuites.data());
}

void RenderRunner::Flush(Vulkan::CommandBuffer& cmd,
                         RenderQueue& queue,
                         const RenderContext& context,
                         RendererFlushFlags flush) const {
	queue.Sort();
	FlushSubset(cmd, queue, context, 0, 1, flush);
}

void RenderRunner::Flush(Vulkan::CommandBuffer& cmd,
                         const RenderQueue& queue,
                         const RenderContext& context,
                         RendererFlushFlags flush) const {
	FlushSubset(cmd, queue, context, 0, 1, flush);
}

void RenderRunner::FlushSubset(Vulkan::CommandBuffer& cmd,
                               const RenderQueue& queue,
                               const RenderContext& context,
                               uint32_t subsetIndex,
                               uint32_t subsetCount,
                               RendererFlushFlags flush) const {
	// Bind global uniforms
	CameraParameters* camera = cmd.AllocateTypedUniformData<CameraParameters>(0, 0, 1);
	*camera                  = context.GetCamera();

	cmd.SetOpaqueState();

	if (flush & RendererFlushFlagBits::FrontFaceClockwise) { cmd.SetFrontFace(vk::FrontFace::eClockwise); }
	if (flush & RendererFlushFlagBits::NoColor) { cmd.SetColorWriteMask(0); }
	if (flush & RendererFlushFlagBits::DepthStencilReadOnly) {
		cmd.SetDepthTest(true);
		cmd.SetDepthWrite(false);
	}
	if (flush & RendererFlushFlagBits::Backface) {
		cmd.SetCullMode(vk::CullModeFlagBits::eFront);
		cmd.SetDepthCompareOp(vk::CompareOp::eGreater);
	}
	if (flush & RendererFlushFlagBits::DepthTestEqual) {
		cmd.SetDepthCompareOp(vk::CompareOp::eEqual);
	} else if (flush & RendererFlushFlagBits::DepthTestInvert) {
		cmd.SetDepthCompareOp(vk::CompareOp::eGreater);
	}

	auto state =
		cmd.SaveState(Vulkan::CommandBufferSaveStateFlagBits::Scissor | Vulkan::CommandBufferSaveStateFlagBits::Viewport |
	                Vulkan::CommandBufferSaveStateFlagBits::RenderState);

	queue.DispatchSubset(RenderQueueType::Opaque, cmd, state, subsetIndex, subsetCount);
	queue.DispatchSubset(RenderQueueType::OpaqueEmissive, cmd, state, subsetIndex, subsetCount);

	if (_type == RendererType::GeneralForward) {
		cmd.RestoreState(state);
		cmd.SetBlendEnable(true);
		cmd.SetColorBlend(vk::BlendFactor::eSrcAlpha, vk::BlendOp::eAdd, vk::BlendFactor::eOneMinusSrcAlpha);
		cmd.SetDepthTest(true);
		cmd.SetDepthWrite(false);
		state =
			cmd.SaveState(Vulkan::CommandBufferSaveStateFlagBits::Scissor | Vulkan::CommandBufferSaveStateFlagBits::Viewport |
		                Vulkan::CommandBufferSaveStateFlagBits::RenderState);

		queue.DispatchSubset(RenderQueueType::Transparent, cmd, state, subsetIndex, subsetCount);
	}
}

void RenderRunner::SetMeshRendererOptions(RendererOptionFlags options) {
	if (options == _options) { return; }
	_options = options;
	UpdateShaderDefines();
}

std::vector<std::pair<std::string, int>> RenderRunner::BuildShaderDefines() {
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

void RenderRunner::SetupShaderSuite() {
	ShaderSuiteResolver defaultResolver;
	for (uint32_t i = 0; i < RenderableTypeCount; ++i) {
		defaultResolver.Resolve(_shaderSuites[i], _type, static_cast<RenderableType>(i));
	}
}

void RenderRunner::UpdateShaderDefines() {
	auto globalDefines = BuildShaderDefines();

	auto& mesh            = _shaderSuites[int(RenderableType::Mesh)];
	mesh.GetBaseDefines() = globalDefines;
	mesh.BakeBaseDefines();
}
}  // namespace Luna
