#pragma once

#include <Luna/Vulkan/Common.hpp>
#include <filesystem>

class Environment {
 public:
	Environment(Luna::Vulkan::Device& device, const std::filesystem::path& envPath);

	Luna::Vulkan::ImageHandle Skybox;
	Luna::Vulkan::ImageHandle Irradiance;
	Luna::Vulkan::ImageHandle Prefiltered;
	Luna::Vulkan::ImageHandle BrdfLut;
};
