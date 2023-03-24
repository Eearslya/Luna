#pragma once

#include <Luna/Renderer/Common.hpp>
#include <Luna/Renderer/RenderGraph.hpp>
#include <Luna/Renderer/RenderPass.hpp>
#include <Luna/Renderer/RenderQueue.hpp>
#include <Luna/Renderer/RenderScene.hpp>
#include <Luna/Scene/Scene.hpp>

#include "Environment.hpp"
#include "Model.hpp"

enum class SceneRendererFlagBits { ForwardOpaque = 1 << 0, ForwardTransparent = 1 << 1, ForwardZPrePass = 1 << 2 };
using SceneRendererFlags = Luna::Bitmask<SceneRendererFlagBits>;
template <>
struct Luna::EnableBitmaskOperators<SceneRendererFlagBits> : std::true_type {};

class SceneRenderer : public Luna::RenderPassInterface {
 public:
	SceneRenderer(Luna::RenderContext& context, Luna::RendererSuite& suite, SceneRendererFlags flags, Luna::Scene& scene);
	~SceneRenderer() noexcept;

	virtual bool GetClearColor(uint32_t attachment, vk::ClearColorValue* value) const override;
	virtual bool GetClearDepthStencil(vk::ClearDepthStencilValue* value) const override;

	virtual void BuildRenderPass(Luna::Vulkan::CommandBuffer& cmd) override;
	virtual void EnqueuePrepareRenderPass(Luna::RenderGraph& graph, Luna::TaskComposer& composer) override;

 private:
	Luna::RenderContext& _context;
	Luna::RendererSuite& _suite;
	SceneRendererFlags _flags;
	Luna::Scene& _scene;
	Luna::RenderScene _renderScene;

	Luna::RenderQueue _queue;
	Luna::VisibilityList _visibleOpaque;
};
