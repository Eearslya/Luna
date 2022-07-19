#include "UI.hpp"

#include <imgui_internal.h>

#include <Vulkan/Image.hpp>

using namespace Luna;

namespace UI {
ImTextureID TextureID(const Vulkan::ImageHandle& image) {
	return reinterpret_cast<ImTextureID>(const_cast<Vulkan::ImageView*>(&image->GetView()));
}
}  // namespace UI
