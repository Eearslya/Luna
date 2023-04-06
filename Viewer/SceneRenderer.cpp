#include "SceneRenderer.hpp"

#include <Luna/Renderer/RenderContext.hpp>
#include <Luna/Renderer/Renderable.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Renderer/RendererSuite.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Image.hpp>

SceneRenderer::SceneRenderer(Luna::RenderContext& context,
                             Luna::RendererSuite& suite,
                             SceneRendererFlags flags,
                             Luna::Scene& scene)
		: _context(context), _suite(suite), _flags(flags), _scene(scene), _renderScene(scene) {}

SceneRenderer::~SceneRenderer() noexcept {}

bool SceneRenderer::GetClearColor(uint32_t attachment, vk::ClearColorValue* value) const {
	if (value) { *value = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f); }

	return true;
}

bool SceneRenderer::GetClearDepthStencil(vk::ClearDepthStencilValue* value) const {
	if (value) { *value = vk::ClearDepthStencilValue(1.0f, 0); }

	return true;
}

void SceneRenderer::BuildRenderPass(Luna::Vulkan::CommandBuffer& cmd) {
	if (_flags & SceneRendererFlagBits::ForwardZPrePass) {}

	if (_flags & SceneRendererFlagBits::ForwardOpaque) {
		_suite.GetRenderer(Luna::RendererSuiteType::ForwardOpaque).Flush(cmd, _queue, _context);
	}

	if (_flags & SceneRendererFlagBits::ForwardTransparent) {}
}

void SceneRenderer::EnqueuePrepareRenderPass(Luna::RenderGraph& graph, Luna::TaskComposer& composer) {
	auto& setup = composer.BeginPipelineStage();
	setup.Enqueue([&]() {
		_visibleOpaque.clear();

		if (_flags & SceneRendererFlagBits::ForwardOpaque) {
			_suite.GetRenderer(Luna::RendererSuiteType::ForwardOpaque).Begin(_queue);
		}
	});

	if (_flags & (SceneRendererFlagBits::ForwardOpaque | SceneRendererFlagBits::ForwardZPrePass)) {
		// Gather Opaque renderables
		auto& gather = composer.BeginPipelineStage();
		// gather.Enqueue([&]() { _renderScene.GatherOpaqueRenderables(_visibleOpaque); });

		if (_flags & SceneRendererFlagBits::ForwardOpaque) {
			// Push Opaque renderables
			auto& push = composer.BeginPipelineStage();
			// push.Enqueue([&]() { _queue.PushRenderables(_context, _visibleOpaque); });
		}
	}

	if (_flags & SceneRendererFlagBits::ForwardTransparent) {}
}
