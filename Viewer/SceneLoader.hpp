#pragma once

#include <Luna/Scene/Scene.hpp>
#include <Luna/Utility/Path.hpp>
#include <Luna/Vulkan/Common.hpp>

class SceneLoader {
 public:
	static void LoadGltf(Luna::Vulkan::Device& device, Luna::Scene& scene, const Luna::Path& gltfPath);
};
