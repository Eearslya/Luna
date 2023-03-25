#pragma once

#include <Luna/Renderer/Common.hpp>

namespace Luna {
struct Texture {
	Luna::Vulkan::ImageHandle Image;
	Luna::Vulkan::Sampler* Sampler = nullptr;
};

class Material : public IntrusivePtrEnabled<Material> {
 public:
	Texture Albedo;
	Texture Normal;
	Texture PBR;
	Texture Occlusion;
	Texture Emissive;
	bool DualSided = false;
};
}  // namespace Luna
