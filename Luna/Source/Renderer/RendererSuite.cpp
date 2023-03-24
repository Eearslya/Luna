#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Renderer/RendererSuite.hpp>

namespace Luna {
RendererSuite::RendererSuite(Vulkan::Device& device) : _device(device) {
	SetRenderer(RendererSuiteType::ForwardOpaque, MakeHandle<Renderer>(_device, RendererType::GeneralForward));
}

RendererSuite::~RendererSuite() noexcept {}

Renderer& RendererSuite::GetRenderer(RendererSuiteType type) {
	return *_renderers[int(type)];
}

const Renderer& RendererSuite::GetRenderer(RendererSuiteType type) const {
	return *_renderers[int(type)];
}

void RendererSuite::SetRenderer(RendererSuiteType type, RendererHandle handle) {
	_renderers[int(type)] = handle;
}
}  // namespace Luna
