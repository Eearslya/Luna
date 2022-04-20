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

	void SetEditorLayout(bool enabled);
	virtual void Update() override;

	Delegate<void()> OnUiRender;

 private:
	bool BeginFrame();
	void EndFrame();

	void DrawRenderSettings();
	void LoadShaders();

	struct DefaultImages {
		Vulkan::ImageHandle Black2D;
		Vulkan::ImageHandle Gray2D;
		Vulkan::ImageHandle Normal2D;
		Vulkan::ImageHandle White2D;
		Vulkan::ImageHandle BlackCube;
		Vulkan::ImageHandle GrayCube;
		Vulkan::ImageHandle WhiteCube;
	};

	struct GBuffer {
		vk::Extent2D Extent;
		Vulkan::ImageHandle Position;
		Vulkan::ImageHandle Normal;
		Vulkan::ImageHandle Albedo;
		Vulkan::ImageHandle PBR;
		Vulkan::ImageHandle Emissive;
	};

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
	struct LightData {
		glm::vec4 Position = glm::vec4(0);
		glm::vec4 Color    = glm::vec4(1);
	};
	struct LightsData {
		LightData Lights[32] = {};
		int LightCount       = 0;
	};

	std::unique_ptr<Vulkan::Context> _context;
	std::unique_ptr<Vulkan::Device> _device;
	std::unique_ptr<Vulkan::Swapchain> _swapchain;
	std::unique_ptr<AssetManager> _assetManager;
	std::unique_ptr<ImGuiManager> _imgui;
	DefaultImages _defaultImages;

	Camera _camera;
	Scene _scene;
	bool _editorLayout       = false;
	bool _performanceQueries = false;
	std::vector<Vulkan::ImageHandle> _sceneImages;
	Vulkan::Program* _program         = nullptr;
	Vulkan::Program* _programGBuffer  = nullptr;
	Vulkan::Program* _programDeferred = nullptr;
	Vulkan::Program* _programGamma    = nullptr;
	Vulkan::Program* _programSkybox   = nullptr;
	Vulkan::BufferHandle _cameraBuffer;
	Vulkan::BufferHandle _sceneBuffer;
	Vulkan::BufferHandle _lightsBuffer;
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
