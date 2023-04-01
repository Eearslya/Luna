#pragma once

#include <Luna/Scene/Scene.hpp>
#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace Luna {
struct TransformComponent;

class Entity {
 public:
	Entity() = default;
	Entity(entt::entity handle, Scene& scene);

	template <typename T, typename... Args>
	T& AddComponent(Args&&... args) {
		return _scene->_registry.emplace<T>(_handle, std::forward<Args>(args)...);
	}
	template <typename T>
	T& GetComponent() {
		return _scene->_registry.get<T>(_handle);
	}
	template <typename T>
	const T& GetComponent() const {
		return _scene->_registry.get<T>(_handle);
	}
	template <typename T>
	bool HasComponent() const {
		return _scene->_registry.try_get<T>(_handle) != nullptr;
	}
	template <typename T>
	void RemoveComponent() {
		_scene->_registry.remove<T>(_handle);
	}

	std::vector<Entity> GetChildren() const;
	glm::mat4 GetGlobalTransform() const;
	glm::mat4 GetLocalTransform() const;
	Entity GetParent() const;

	void Rotate(const glm::vec3& rDelta);
	void Scale(const glm::vec3& sDelta);
	void Scale(float sDelta);
	void SetParent(Entity newParent);
	void Translate(const glm::vec3& tDelta);

	TransformComponent& Transform();
	const TransformComponent& Transform() const;

	operator entt::entity() {
		return _handle;
	}
	operator const entt::entity() const {
		return _handle;
	}

	operator bool() const {
		return _scene && _scene->_registry.valid(_handle);
	}
	bool operator==(const Entity& other) const {
		return _handle == other._handle && _scene == other._scene;
	}
	bool operator!=(const Entity& other) const {
		return _handle != other._handle || _scene != other._scene;
	}

 private:
	entt::entity _handle = entt::null;
	Scene* _scene        = nullptr;
};
}  // namespace Luna
