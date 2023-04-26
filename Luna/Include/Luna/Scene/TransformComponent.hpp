#pragma once

#include <Luna/Scene/Component.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace Luna {
struct TransformComponent final : public Component {
	TransformComponent()                          = default;
	TransformComponent(const TransformComponent&) = default;

	glm::mat4 GetTransform() const {
		glm::mat4 matrix = glm::translate(glm::mat4(1.0f), Translation);
		matrix *= glm::mat4(glm::quat(glm::radians(Rotation)));
		matrix = glm::scale(matrix, Scale);

		return matrix;
	}

	glm::vec3 Translation = glm::vec3(0.0f, 0.0f, 0.0f);
	glm::vec3 Rotation    = glm::vec3(0.0f, 0.0f, 0.0f);
	glm::vec3 Scale       = glm::vec3(1.0f, 1.0f, 1.0f);

	bool LockScale = true;

	virtual bool Deserialize(const nlohmann::json& data) override {
		Translation = data.at("Translation").get<glm::vec3>();
		Rotation    = data.at("Rotation").get<glm::vec3>();
		Scale       = data.at("Scale").get<glm::vec3>();

		return true;
	}

	virtual void Serialize(nlohmann::json& data) const override {
		data["Translation"] = Translation;
		data["Rotation"]    = Rotation;
		data["Scale"]       = Scale;
	}
};
}  // namespace Luna
