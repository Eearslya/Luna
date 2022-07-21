#pragma once

#include "Vulkan/Image.hpp"
#include "Vulkan/Sampler.hpp"

namespace Luna {
struct Texture : public IntrusivePtrEnabled<Texture> {
	Vulkan::ImageHandle Image;
	Vulkan::Sampler* Sampler = nullptr;
};
using TextureHandle = IntrusivePtr<Texture>;
}  // namespace Luna
