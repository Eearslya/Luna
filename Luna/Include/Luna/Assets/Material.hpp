#pragma once

#include <Luna/Assets/Asset.hpp>
#include <glm/glm.hpp>

namespace Luna {
class Material : public Asset {
 public:
	static AssetType GetAssetType() {
		return AssetType::Material;
	}

	struct MaterialData {
		glm::vec4 BaseColorFactor = glm::vec4(1, 1, 1, 1);
		glm::vec4 EmissiveFactor  = glm::vec4(0, 0, 0, 1);
		float AlphaCutoff         = 0.5f;
		float MetallicFactor      = 0.0f;
		float RoughnessFactor     = 0.5f;
	};

	MaterialData Data() const {
		return MaterialData{.BaseColorFactor = BaseColorFactor,
		                    .EmissiveFactor  = glm::vec4(EmissiveFactor, 1.0f),
		                    .AlphaCutoff     = AlphaCutoff,
		                    .MetallicFactor  = MetallicFactor,
		                    .RoughnessFactor = RoughnessFactor};
	}

	glm::vec4 BaseColorFactor = glm::vec4(1, 1, 1, 1);
	glm::vec3 EmissiveFactor  = glm::vec3(0, 0, 0);
	float AlphaCutoff         = 0.5f;
	float MetallicFactor      = 0.0f;
	float RoughnessFactor     = 0.5f;
};
}  // namespace Luna
