#include "SceneSerializer.hpp"

#include <yaml-cpp/yaml.h>

#include <fstream>

#include "CameraComponent.hpp"
#include "Entity.hpp"
#include "MeshComponent.hpp"
#include "NameComponent.hpp"
#include "RelationshipComponent.hpp"
#include "Scene.hpp"
#include "TransformComponent.hpp"
#include "Utility/Files.hpp"

namespace YAML {
template <>
struct convert<glm::vec3> {
	static Node encode(const glm::vec3& v) {
		Node node;
		node.push_back(v.x);
		node.push_back(v.y);
		node.push_back(v.z);
		return node;
	}

	static bool decode(const Node& node, glm::vec3& v) {
		if (!node.IsSequence() || node.size() != 3) { return false; }

		v.x = node[0].as<float>();
		v.y = node[1].as<float>();
		v.z = node[2].as<float>();

		return true;
	}
};
}  // namespace YAML

namespace Luna {
YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec3& v) {
	out << YAML::Flow;
	out << YAML::BeginSeq << v.x << v.y << v.z << YAML::EndSeq;
	return out;
}

SceneSerializer::SceneSerializer(Scene& scene) : _scene(scene) {}

static void SerializeEntity(YAML::Emitter& out, Entity entity) {
	out << YAML::BeginMap;
	out << YAML::Key << "Entity" << YAML::Value << "123";

	if (entity.HasComponent<NameComponent>()) {
		auto& cName = entity.GetComponent<NameComponent>();

		out << YAML::Key << "NameComponent";
		out << YAML::BeginMap;
		out << YAML::Key << "Name" << YAML::Value << cName.Name;
		out << YAML::EndMap;
	}

	if (entity.HasComponent<TransformComponent>()) {
		auto& cTransform = entity.GetComponent<TransformComponent>();
		out << YAML::Key << "TransformComponent";
		out << YAML::BeginMap;
		out << YAML::Key << "Translation" << YAML::Value << cTransform.Translation;
		out << YAML::Key << "Rotation" << YAML::Value << cTransform.Rotation;
		out << YAML::Key << "Scale" << YAML::Value << cTransform.Scale;
		out << YAML::EndMap;
	}

	if (entity.HasComponent<CameraComponent>()) {
		auto& cCamera = entity.GetComponent<CameraComponent>();

		out << YAML::Key << "CameraComponent";
		out << YAML::BeginMap;
		out << YAML::Key << "Camera" << YAML::Value << YAML::BeginMap;
		out << YAML::Key << "FovDegrees" << YAML::Value << cCamera.Camera.GetFovDegrees();
		out << YAML::Key << "ZNear" << YAML::Value << cCamera.Camera.GetZNear();
		out << YAML::Key << "ZFar" << YAML::Value << cCamera.Camera.GetZFar();
		out << YAML::EndMap;
		out << YAML::Key << "Primary" << YAML::Value << cCamera.Primary;
		out << YAML::EndMap;
	}

	if (entity.HasComponent<MeshComponent>()) {
		auto& cMesh = entity.GetComponent<MeshComponent>();

		out << YAML::Key << "MeshComponent";
		out << YAML::BeginMap;
		out << YAML::EndMap;
	}

	out << YAML::EndMap;
}

void SceneSerializer::Serialize(const std::filesystem::path& filePath) {
	YAML::Emitter out;
	out << YAML::BeginMap;
	out << YAML::Key << "Scene" << YAML::Value << "Name";
	out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;
	_scene._registry.each([&](auto entityId) {
		Entity entity(entityId, _scene);
		if (!entity) { return; }

		SerializeEntity(out, entity);
	});
	out << YAML::EndSeq;
	out << YAML::EndMap;

	const auto fileDir = filePath.parent_path();
	if (!std::filesystem::exists(fileDir)) { std::filesystem::create_directories(fileDir); }
	std::ofstream file(filePath);
	file << out.c_str();
}

bool SceneSerializer::Deserialize(const std::filesystem::path& filePath) {
	auto dataStr    = ReadFile(filePath);
	YAML::Node data = YAML::Load(dataStr);

	if (!data["Scene"]) { return false; }

	_scene.Clear();
	auto entities = data["Entities"];
	if (entities) {
		for (auto entity : entities) {
			std::string name;
			auto nameComponent = entity["NameComponent"];
			if (nameComponent) { name = nameComponent["Name"].as<std::string>(); }

			Entity e = _scene.CreateEntity(name);

			auto transformComponent = entity["TransformComponent"];
			if (transformComponent) {
				auto& cTransform       = e.GetComponent<TransformComponent>();
				cTransform.Translation = transformComponent["Translation"].as<glm::vec3>();
				cTransform.Rotation    = transformComponent["Rotation"].as<glm::vec3>();
				cTransform.Scale       = transformComponent["Scale"].as<glm::vec3>();
			}

			auto cameraComponent = entity["CameraComponent"];
			if (cameraComponent) {
				auto& cCamera = e.AddComponent<CameraComponent>();
				auto camera   = cameraComponent["Camera"];
				if (camera) {
					float fovDegrees = camera["FovDegrees"].as<float>();
					float zNear      = camera["ZNear"].as<float>();
					float zFar       = camera["ZFar"].as<float>();
					cCamera.Camera.SetPerspective(fovDegrees, zNear, zFar);
				}
				cCamera.Primary = cameraComponent["Primary"].as<bool>();
			}

			auto meshComponent = entity["MeshComponent"];
			if (meshComponent) { auto& cMesh = e.AddComponent<MeshComponent>(); }
		}
	}

	return true;
}
}  // namespace Luna
