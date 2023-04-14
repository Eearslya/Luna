#pragma once

#include <Luna/Renderer/Common.hpp>

namespace Luna {
class Renderable : public IntrusivePtrEnabled<Renderable> {
 public:
	virtual ~Renderable() noexcept;

	virtual void Enqueue(const RenderContext& context, const RenderableInfo& self, RenderQueue& queue) const = 0;
	virtual void EnqueueDepth(const RenderContext& context, const RenderableInfo& self, RenderQueue& queue) const {
		Enqueue(context, self, queue);
	}
	virtual void Render(Vulkan::CommandBuffer& cmd) const = 0;
};
}  // namespace Luna
