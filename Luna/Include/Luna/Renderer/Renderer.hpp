#pragma once

#include <Luna/Renderer/Common.hpp>
#include <Luna/Renderer/ShaderSuite.hpp>

namespace Luna {
class Renderer : public IntrusivePtrEnabled<Renderer> {
 public:
	Renderer(Vulkan::Device& device, RendererType type);
	virtual ~Renderer() noexcept = default;

	void Begin(RenderQueue& queue) const;
	void Flush(Vulkan::CommandBuffer& cmd, RenderQueue& queue, const RenderContext& context) const;
	void Flush(Vulkan::CommandBuffer& cmd, const RenderQueue& queue, const RenderContext& context) const;
	void FlushSubset(Vulkan::CommandBuffer& cmd,
	                 const RenderQueue& queue,
	                 const RenderContext& context,
	                 uint32_t subsetIndex,
	                 uint32_t subsetCount) const;
	void SetMeshRendererOptions(RendererOptionFlags options);

 protected:
	mutable std::array<ShaderSuite, RenderableTypeCount> _shaderSuites;

 private:
	std::vector<std::pair<std::string, int>> BuildShaderDefines();
	void SetupShaderSuite();
	void UpdateShaderDefines();

	Vulkan::Device& _device;
	RendererType _type;
	RendererOptionFlags _options = {};
};
}  // namespace Luna
