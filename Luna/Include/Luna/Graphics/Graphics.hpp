#pragma once

#include <Luna/Devices/Window.hpp>
#include <Luna/Filesystem/Filesystem.hpp>
#include <Luna/Graphics/ImGuiManager.hpp>
#include <Luna/Input/Input.hpp>
#include <Luna/Scene/Camera.hpp>
#include <Luna/Scene/Scene.hpp>
#include <Luna/Scene/StaticMesh.hpp>
#include <Luna/Threading/Threading.hpp>
#include <Luna/Utility/Delegate.hpp>
#include <glm/glm.hpp>

namespace Luna {
namespace Vulkan {
class Context;
class Device;
}  // namespace Vulkan

class Graphics : public Module::Registrar<Graphics> {
	static inline const bool Registered =
		Register("Graphics", Stage::Render, Depends<Filesystem, Input, Threading, Window>());

 public:
	Graphics();
	~Graphics() noexcept;

	Delegate<void()>& OnUiRender() {
		return _onUiRender;
	}

	virtual void Update() override;

 private:
	bool BeginFrame();
	void EndFrame();

	void LoadShaders();

	struct CameraData {
		glm::mat4 Projection;
		glm::mat4 View;
		glm::vec3 Position;
	};
	struct SceneData {
		glm::vec4 SunDirection;
	};

	std::unique_ptr<Vulkan::Context> _context;
	std::unique_ptr<Vulkan::Device> _device;
	std::unique_ptr<Vulkan::Swapchain> _swapchain;
	std::unique_ptr<ImGuiManager> _imgui;
	Delegate<void()> _onUiRender;
	Vulkan::ImageHandle _whiteImage;

	Camera _camera;
	Scene _scene;
	Vulkan::Program* _program = nullptr;
	StaticMesh _mesh;
	Vulkan::BufferHandle _positionBuffer;
	Vulkan::BufferHandle _normalBuffer;
	Vulkan::BufferHandle _indexBuffer;
	Vulkan::BufferHandle _texcoordBuffer;
	Vulkan::BufferHandle _cameraBuffer;
	Vulkan::BufferHandle _sceneBuffer;
	uint64_t _indexCount = 0;
	bool _mouseControl   = false;
};
}  // namespace Luna
