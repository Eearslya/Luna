#pragma once

#include <Luna/Devices/Window.hpp>
#include <Luna/Filesystem/Filesystem.hpp>
#include <Luna/Graphics/ImGuiManager.hpp>
#include <Luna/Input/Input.hpp>
#include <Luna/Scene/Camera.hpp>
#include <Luna/Scene/Scene.hpp>
#include <Luna/Threading/Threading.hpp>
#include <Luna/Utility/Delegate.hpp>
#include <glm/glm.hpp>
//
#include <ImGuizmo.h>

namespace Luna {
namespace Vulkan {
class Context;
class Device;
}  // namespace Vulkan

class AssetManager;

class Graphics : public Module::Registrar<Graphics> {
	static inline const bool Registered =
		Register("Graphics", Stage::Render, Depends<Filesystem, Input, Threading, Window>());

 public:
	Graphics();
	~Graphics() noexcept;

	AssetManager& GetAssetManager() {
		return *_assetManager;
	}
	Vulkan::Device& GetDevice() {
		return *_device;
	}
	Scene& GetScene() {
		return _scene;
	}
	bool IsEditorLayout() const {
		return _editorLayout;
	}
	Delegate<void()>& OnUiRender() {
		return _onUiRender;
	}

	void SetEditorLayout(bool enabled);
	virtual void Update() override;

 private:
	bool BeginFrame();
	void EndFrame();

	void DrawRenderSettings();
	void LoadShaders();

	struct CameraData {
		glm::mat4 Projection;
		glm::mat4 View;
		glm::mat4 ViewInverse;
		glm::vec3 Position;
	};
	struct SceneData {
		glm::vec4 SunDirection;
		float PrefilteredCubeMipLevels;
		float Exposure;
		float Gamma;
		float IBLContribution;
	};

	std::unique_ptr<Vulkan::Context> _context;
	std::unique_ptr<Vulkan::Device> _device;
	std::unique_ptr<Vulkan::Swapchain> _swapchain;
	std::unique_ptr<AssetManager> _assetManager;
	std::unique_ptr<ImGuiManager> _imgui;
	Delegate<void()> _onUiRender;
	Vulkan::ImageHandle _whiteImage;

	Camera _camera;
	Scene _scene;
	bool _editorLayout = false;
	Vulkan::ImageHandle _sceneImage;
	Vulkan::Program* _program       = nullptr;
	Vulkan::Program* _programSkybox = nullptr;
	Vulkan::BufferHandle _cameraBuffer;
	Vulkan::BufferHandle _sceneBuffer;
	bool _mouseControl      = false;
	glm::vec3 _sunDirection = glm::normalize(glm::vec3(1.0f, 2.0f, 0.5f));
	glm::vec3 _sunPosition;

	bool _drawSkybox             = true;
	int _pbrDebug                = 0;
	int _skyDebug                = 0;
	float _exposure              = 4.5f;
	float _gamma                 = 2.2f;
	float _iblContribution       = 1.0f;
	ImGuizmo::OPERATION _gizmoOp = ImGuizmo::TRANSLATE;
	ImGuizmo::MODE _gizmoMode    = ImGuizmo::LOCAL;
};
}  // namespace Luna
