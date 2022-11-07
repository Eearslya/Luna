#include <Luna/Vulkan/WSI.hpp>

namespace Luna {
namespace Vulkan {
WSI::WSI(WSIPlatform* platform) : _platform(platform) {}

WSI::~WSI() noexcept {}

void WSI::BeginFrame() {}

void WSI::EndFrame() {}

bool WSI::IsAlive() {
	return _platform && _platform->IsAlive();
}

void WSI::Update() {
	if (_platform) { _platform->Update(); }
}
}  // namespace Vulkan
}  // namespace Luna
