#pragma once

#include <Luna/Renderer/Common.hpp>
#include <Luna/Renderer/Renderable.hpp>

namespace Luna {
struct StaticSubmeshRenderInfo {
	Vulkan::Program* Program = nullptr;
};

struct StaticSubmeshInstanceInfo {
	glm::mat4 Model;
};

class StaticSubmesh : public Renderable {
 public:
	virtual void Enqueue(const RenderContext& context, const RenderableInfo& self, RenderQueue& queue) const override;
};

class StaticMesh : public IntrusivePtrEnabled<StaticMesh> {
 public:
	std::vector<IntrusivePtr<StaticSubmesh>> GatherOpaque() const;

 private:
	std::vector<IntrusivePtr<StaticSubmesh>> _submeshes;
};
}  // namespace Luna
