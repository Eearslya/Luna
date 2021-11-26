#include <Luna/Graphics/Graphics.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>

namespace Luna {
Graphics::Graphics() {
	const auto instanceExtensions = Window::Get()->GetRequiredInstanceExtensions();
	_context                      = std::make_unique<Vulkan::Context>(instanceExtensions);
	_device                       = std::make_unique<Vulkan::Device>(*_context);
}

Graphics::~Graphics() noexcept {}

void Graphics::Update() {
	auto cmd = _device->RequestCommandBuffer();
	_device->Submit(cmd);

	_device->NextFrame();
}
}  // namespace Luna
