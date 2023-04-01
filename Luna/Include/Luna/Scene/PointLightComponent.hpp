#pragma once

#include <Luna/Scene/Entity.hpp>
#include <Luna/Utility/IntrusivePtr.hpp>
#include <glm/glm.hpp>

namespace Luna {
struct PointLightData {
	glm::vec3 Position;
	float Multiplier;
	glm::vec3 Radiance;
	float MinRadius;
	float Radius;
	float Falloff;
	float LightSize;
};

class PointLightComponent {
 public:
	PointLightComponent() = default;

	PointLightData Data(const Luna::Entity& entity) const {
		PointLightData data = {};
		data.Position       = entity.GetGlobalTransform()[3];
		data.Multiplier     = Multiplier;
		data.Radiance       = Radiance;
		data.MinRadius      = MinRadius;
		data.Radius         = Radius;
		data.Falloff        = Falloff;
		data.LightSize      = LightSize;

		return data;
	}

	glm::vec3 Radiance = glm::vec3(1, 1, 1);
	float Falloff      = 1.0f;
	float LightSize    = 0.5f;
	float MinRadius    = 1.0f;
	float Multiplier   = 1.0f;
	float Radius       = 10.0f;
};
}  // namespace Luna
