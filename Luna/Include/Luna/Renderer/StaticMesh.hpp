#pragma once

#include <Luna/Assets/Asset.hpp>
#include <Luna/Assets/Mesh.hpp>
#include <Luna/Renderer/Renderable.hpp>

namespace Luna {
class StaticMesh final : public Renderable {
 public:
	StaticMesh(IntrusivePtr<Mesh> mesh, uint32_t submeshIndex);

	virtual void Enqueue(const RenderContext& context, const RenderableInfo& self, RenderQueue& queue) const override;
	virtual void Render(Vulkan::CommandBuffer& cmd) const override;

 private:
	IntrusivePtr<Mesh> _mesh;
	uint32_t _submeshIndex;
};
}  // namespace Luna
