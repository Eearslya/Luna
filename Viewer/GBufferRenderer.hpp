#pragma once

#include <Luna/Renderer/RenderPass.hpp>
#include <Luna/Renderer/RenderScene.hpp>

namespace Luna {
class RenderContext;
class Scene;
}  // namespace Luna

class GBufferRenderer : public Luna::RenderPassInterface {
 public:
	GBufferRenderer(const Luna::RenderContext& context, Luna::Scene& scene);

	virtual bool GetClearColor(uint32_t attachment, vk::ClearColorValue* value) const override;

	virtual void BuildRenderPass(Luna::Vulkan::CommandBuffer& cmd) override;
	virtual void EnqueuePrepareRenderPass(Luna::RenderGraph& graph, Luna::TaskComposer& composer) override;

 private:
	const Luna::RenderContext& _context;
	Luna::Scene& _scene;
	Luna::RenderScene _renderScene;
};
