#pragma once

#include <filesystem>

namespace Luna {
class Scene;

class SceneSerializer {
 public:
	SceneSerializer(Scene& scene);

	void Serialize(const std::filesystem::path& filePath);

	bool Deserialize(const std::filesystem::path& filePath);

 private:
	Scene& _scene;
};
}  // namespace Luna
