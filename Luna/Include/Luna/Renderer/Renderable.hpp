#pragma once

#include <Luna/Renderer/Common.hpp>

namespace Luna {
class Renderable : public IntrusivePtrEnabled<Renderable> {
 public:
	virtual ~Renderable() noexcept;

	virtual void Enqueue(const RenderContext& context, const RenderableInfo& self, RenderQueue& queue) const = 0;
};
}  // namespace Luna
