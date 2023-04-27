#pragma once

#include <Luna/Renderer/RenderPass.hpp>
#include <Luna/Renderer/RenderQueue.hpp>

namespace Luna {
class RenderContext;

enum class SceneRendererFlagBits {
	ForwardOpaque      = 1 << 0,
	ForwardTransparent = 1 << 1,
	ForwardZPrePass    = 1 << 2,
	DeferredGBuffer    = 1 << 3,
	DeferredLighting   = 1 << 4,
	Depth              = 1 << 5
};
using SceneRendererFlags = Bitmask<SceneRendererFlagBits>;

class SceneRenderer : public RenderPassInterface {
 public:
	SceneRenderer(const RenderContext& context, SceneRendererFlags flags);

	virtual bool GetClearColor(uint32_t attachment, vk::ClearColorValue* value) const override;
	virtual bool GetClearDepthStencil(vk::ClearDepthStencilValue* value) const override;
	virtual void BuildRenderPass(Vulkan::CommandBuffer& cmd) override;
	virtual void EnqueuePrepareRenderPass(RenderGraph& graph, TaskComposer& composer) override;

 private:
	const RenderContext& _context;
	SceneRendererFlags _flags;

	RenderQueue _depthQueue;
	RenderQueue _opaqueQueue;
	RenderQueue _transparentQueue;

	VisibilityList _opaqueVisible;
	VisibilityList _transparentVisible;
};
}  // namespace Luna

template <>
struct Luna::EnableBitmaskOperators<Luna::SceneRendererFlagBits> : std::true_type {};
