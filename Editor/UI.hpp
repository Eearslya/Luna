#pragma once

#include <imgui.h>

#include "IconsFontAwesome6.h"
#include "Vulkan/Common.hpp"

namespace UI {
ImTextureID TextureID(const Luna::Vulkan::ImageHandle& image);
}
