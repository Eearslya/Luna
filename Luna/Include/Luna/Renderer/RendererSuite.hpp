#pragma once

#include <Luna/Renderer/Common.hpp>

namespace Luna {
class RendererSuite {
 public:
	RendererSuite(Vulkan::Device& device);
	RendererSuite(const RendererSuite&)  = delete;
	RendererSuite(RendererSuite&&)       = delete;
	void operator=(const RendererSuite&) = delete;
	void operator=(RendererSuite&&)      = delete;
	~RendererSuite() noexcept;

	Renderer& GetRenderer(RendererSuiteType type);
	const Renderer& GetRenderer(RendererSuiteType type) const;

	void SetRenderer(RendererSuiteType type, RendererHandle handle);

 private:
	Vulkan::Device& _device;
	std::array<RendererHandle, RendererSuiteTypeCount> _renderers;
};
}  // namespace Luna
