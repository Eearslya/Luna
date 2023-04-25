#pragma once

#include <Luna/Renderer/RenderPass.hpp>

namespace Luna {
class RenderContext;

class SceneRenderer : public RenderPassInterface {
 public:
	SceneRenderer(const RenderContext& context);

	virtual bool GetClearColor(uint32_t attachment, vk::ClearColorValue* value) const override;
	virtual bool GetClearDepthStencil(vk::ClearDepthStencilValue* value) const override;
	virtual void BuildRenderPass(Vulkan::CommandBuffer& cmd) override;

 private:
	const RenderContext& _context;
};
}  // namespace Luna
