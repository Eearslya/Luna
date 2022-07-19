#pragma once

#include <imgui.h>

#include <Vulkan/Common.hpp>

#include "IconsFontAwesome6.h"

namespace UI {
ImTextureID TextureID(const Luna::Vulkan::ImageHandle& image);
}
