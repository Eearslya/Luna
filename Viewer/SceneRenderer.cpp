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
	if (_flags & SceneRendererFlagBits::ForwardZPrePass) {
		_suite.GetRenderer(Luna::RendererSuiteType::PrepassDepth)
			.Flush(cmd, _depthQueue, _context, Luna::RendererFlushFlagBits::NoColor);
	}

	if (_flags & SceneRendererFlagBits::ForwardOpaque) {
		Luna::RendererFlushFlags flush = {};
		if (_flags & SceneRendererFlagBits::ForwardZPrePass) {
			flush |= Luna::RendererFlushFlagBits::DepthStencilReadOnly | Luna::RendererFlushFlagBits::DepthTestEqual;
		}
		_suite.GetRenderer(Luna::RendererSuiteType::ForwardOpaque).Flush(cmd, _opaqueQueue, _context, flush);
	}

	if (_flags & SceneRendererFlagBits::ForwardTransparent) {}
}

void SceneRenderer::EnqueuePrepareRenderPass(Luna::RenderGraph& graph, Luna::TaskComposer& composer) {
	const auto& frustum = _context.GetFrustum();

	auto& setup = composer.BeginPipelineStage();
	setup.Enqueue([&]() {
		_opaqueVisible.clear();
		_transparentVisible.clear();

		if (_flags & SceneRendererFlagBits::ForwardZPrePass) {
			_suite.GetRenderer(Luna::RendererSuiteType::PrepassDepth).Begin(_depthQueue);
		} else if (_flags & SceneRendererFlagBits::Depth) {
		}

		if (_flags & SceneRendererFlagBits::ForwardOpaque) {
			_suite.GetRenderer(Luna::RendererSuiteType::ForwardOpaque).Begin(_opaqueQueue);
		} else if (_flags & SceneRendererFlagBits::DeferredGBuffer) {
			_suite.GetRenderer(Luna::RendererSuiteType::Deferred).Begin(_opaqueQueue);
		}

		if (_flags & SceneRendererFlagBits::ForwardTransparent) {
			_suite.GetRenderer(Luna::RendererSuiteType::ForwardTransparent).Begin(_transparentQueue);
		}
	});

	if (_flags & (SceneRendererFlagBits::ForwardOpaque | SceneRendererFlagBits::ForwardZPrePass)) {
		// Gather Opaque renderables
		_renderScene.GatherOpaqueRenderables(composer, frustum, _opaqueVisible);

		if (_flags & SceneRendererFlagBits::ForwardZPrePass) {
			// Push Opaque renderables
			auto& push = composer.BeginPipelineStage();
			push.Enqueue([&]() {
				_depthQueue.PushDepthRenderables(_context, _opaqueVisible);
				_depthQueue.Sort();
			});
		}

		if (_flags & SceneRendererFlagBits::ForwardOpaque) {
			// Push Opaque renderables
			auto& push = composer.BeginPipelineStage();
			push.Enqueue([&]() {
				_opaqueQueue.PushRenderables(_context, _opaqueVisible);
				_opaqueQueue.Sort();
			});
		}
	}

	if (_flags & SceneRendererFlagBits::ForwardTransparent) {}
}
