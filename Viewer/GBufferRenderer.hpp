#pragma once

#include <Luna/Renderer/RenderPass.hpp>
#include <Luna/Renderer/RenderQueue.hpp>
#include <Luna/Renderer/RenderScene.hpp>
#include <Luna/Vulkan/Common.hpp>

namespace Luna {
class RenderContext;
class Scene;
}  // namespace Luna

class GBufferRenderer : public Luna::RenderPassInterface {
 public:
	GBufferRenderer(Luna::RenderContext& context, Luna::Scene& scene);

	virtual bool GetClearColor(uint32_t attachment, vk::ClearColorValue* value) const override;

	virtual void BuildRenderPass(Luna::Vulkan::CommandBuffer& cmd) override;
	virtual void EnqueuePrepareRenderPass(Luna::RenderGraph& graph, Luna::TaskComposer& composer) override;

 private:
	void RenderMeshes(Luna::Vulkan::CommandBuffer& cmd);

	Luna::RenderContext& _context;
	Luna::Scene& _scene;
	Luna::RenderScene _renderScene;
	Luna::RenderQueue _renderQueue;
	Luna::VisibilityList _opaqueList;
	std::vector<Luna::Vulkan::BufferHandle> _materialBuffers;
	std::vector<Luna::Vulkan::BufferHandle> _objectBuffers;
	std::vector<Luna::Vulkan::BufferHandle> _indirectBuffers;
};
