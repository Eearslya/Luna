#pragma once

#include <Luna/Utility/Path.hpp>

namespace Luna {
class Entity;
class Scene;

class Editor final {
 public:
	static bool Initialize();
	static void Update(double deltaTime);
	static void Shutdown();

	static Scene& GetActiveScene();
	static Entity& GetSelectedEntity();
	static void RequestAsset(const Path& assetPath);
	static void SetSelectedEntity(const Entity& entity);
};
}  // namespace Luna
