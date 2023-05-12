#pragma once

#include <Luna/Assets/Asset.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <glm/glm.hpp>

namespace Luna {
struct Texture : public Asset {
 public:
	static AssetType GetAssetType() {
		return AssetType::Texture;
	}

	vk::Format Format;
	glm::uvec2 Size;

	Vulkan::ImageHandle Image;
	std::vector<uint8_t> ImageData;
};
}  // namespace Luna
