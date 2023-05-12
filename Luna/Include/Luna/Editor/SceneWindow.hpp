#pragma once

#include <Luna/Scene/EditorCamera.hpp>
#include <Luna/Utility/Delegate.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace Luna {
class SceneWindow final : public Observer {
 public:
	SceneWindow(int index);
	~SceneWindow() noexcept;

	void Update(double deltaTime);

 private:
	void Invalidate();

	bool _cameraControl  = false;
	int _gizmoMode       = 0;
	bool _focusNextFrame = false;
	int _sceneView       = -1;
	int _windowIndex;
	glm::vec2 _windowSize;
	std::string _windowTitle;
};
}  // namespace Luna
