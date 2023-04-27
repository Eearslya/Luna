#pragma once

#include <Luna/Renderer/Common.hpp>
#include <Luna/Renderer/ShaderSuite.hpp>

namespace Luna {
class RenderRunner : public IntrusivePtrEnabled<RenderRunner> {
 public:
	RenderRunner(RendererType type);
	virtual ~RenderRunner() noexcept = default;

	void Begin(RenderQueue& queue) const;
	void Flush(Vulkan::CommandBuffer& cmd,
	           RenderQueue& queue,
	           const RenderContext& context,
	           RendererFlushFlags flush = {}) const;
	void Flush(Vulkan::CommandBuffer& cmd,
	           const RenderQueue& queue,
	           const RenderContext& context,
	           RendererFlushFlags flush = {}) const;
	void FlushSubset(Vulkan::CommandBuffer& cmd,
	                 const RenderQueue& queue,
	                 const RenderContext& context,
	                 uint32_t subsetIndex,
	                 uint32_t subsetCount,
	                 RendererFlushFlags flush = {}) const;
	void SetMeshRendererOptions(RendererOptionFlags options);

 protected:
	mutable std::array<ShaderSuite, RenderableTypeCount> _shaderSuites;

 private:
	std::vector<std::pair<std::string, int>> BuildShaderDefines();
	void SetupShaderSuite();
	void UpdateShaderDefines();

	RendererType _type;
	RendererOptionFlags _options = {};
};

using RenderRunnerHandle = IntrusivePtr<RenderRunner>;
}  // namespace Luna
