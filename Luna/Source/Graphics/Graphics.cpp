#include <Luna/Graphics/Graphics.hpp>
#include <Luna/Vulkan/Context.hpp>

namespace Luna {
Graphics::Graphics() {
	const auto instanceExtensions = Window::Get()->GetRequiredInstanceExtensions();
	_context                      = std::make_unique<Vulkan::Context>(instanceExtensions);
}

Graphics::~Graphics() noexcept {}

void Graphics::Update() {}
}  // namespace Luna
