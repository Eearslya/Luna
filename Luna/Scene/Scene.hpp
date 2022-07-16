#pragma once

#include <entt/entt.hpp>

namespace Luna {
class Scene {
 public:
	Scene();
	~Scene() noexcept;

 private:
	entt::registry _registry;
};
}  // namespace Luna
