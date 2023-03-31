#pragma once

#include <Luna/Utility/Path.hpp>
#include <Luna/Vulkan/Common.hpp>

namespace Luna {
class Environment : public IntrusivePtrEnabled<Environment> {
 public:
	Environment(Vulkan::Device& device, const Path& envPath);

	Vulkan::ImageHandle Skybox;
	Vulkan::ImageHandle Irradiance;
	Vulkan::ImageHandle Prefiltered;
	Vulkan::ImageHandle BrdfLut;
};
}  // namespace Luna
