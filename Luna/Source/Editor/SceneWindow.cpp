#include <Luna/Core/Input.hpp>
#include <Luna/Editor/Editor.hpp>
#include <Luna/Editor/SceneWindow.hpp>
#include <Luna/Renderer/RenderGraph.hpp>
#include <Luna/Renderer/RenderPass.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Scene/Entity.hpp>
#include <Luna/Scene/Scene.hpp>
#include <Luna/Scene/TransformComponent.hpp>
#include <Luna/UI/UI.hpp>
#include <Luna/UI/UIManager.hpp>
#include <Luna/Utility/Threading.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>

namespace Luna {
SceneWindow::SceneWindow(int index) : _windowIndex(index), _windowSize(0.0f, 0.0f) {
	_sceneView   = Renderer::RegisterSceneView();
	_windowTitle = "Scene##SceneWindow" + std::to_string(_windowIndex);

	Input::OnMouseMoved.Add(
		[this](const glm::dvec2& pos) {
			if (_cameraControl) {
				const float sensitivity = 0.5f;
				_camera.Rotate(pos.y * sensitivity, -(pos.x * sensitivity));
			}
		},
		this);
}

SceneWindow::~SceneWindow() noexcept {
	Renderer::UnregisterSceneView(_sceneView);
}

void SceneWindow::Update(double deltaTime) {
	if (_focusNextFrame) {
		ImGui::SetNextWindowFocus();
		_focusNextFrame = false;
	}
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	const bool open = ImGui::Begin(_windowTitle.c_str());
	ImGui::PopStyleVar();

	if (open) {
		const ImVec2 imWindowSize = ImGui::GetContentRegionAvail();
		const glm::vec2 windowSize(imWindowSize.x, imWindowSize.y);
		if (_windowSize != windowSize) {
			_windowSize = windowSize;
			Invalidate();
		}

		if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { _focusNextFrame = true; }

		const bool sceneActive = ImGui::IsWindowHovered() && ImGui::IsWindowFocused();

		const auto rmb = Input::GetButton(MouseButton::Right);
		if (sceneActive && !_cameraControl && rmb == Luna::InputAction::Press) {
			_cameraControl = true;
			Input::SetCursorHidden(true);
		}
		if (_cameraControl && rmb == Luna::InputAction::Release) {
			_cameraControl = false;
			Input::SetCursorHidden(false);
		}

		if (_cameraControl) {
			const float camSpeed = 3.0f;
			glm::vec3 camMove(0);
			if (Luna::Input::GetKey(Luna::Key::W) == Luna::InputAction::Press) { camMove += glm::vec3(0, 0, 1); }
			if (Luna::Input::GetKey(Luna::Key::S) == Luna::InputAction::Press) { camMove -= glm::vec3(0, 0, 1); }
			if (Luna::Input::GetKey(Luna::Key::A) == Luna::InputAction::Press) { camMove -= glm::vec3(1, 0, 0); }
			if (Luna::Input::GetKey(Luna::Key::D) == Luna::InputAction::Press) { camMove += glm::vec3(1, 0, 0); }
			if (Luna::Input::GetKey(Luna::Key::R) == Luna::InputAction::Press) { camMove += glm::vec3(0, 1, 0); }
			if (Luna::Input::GetKey(Luna::Key::F) == Luna::InputAction::Press) { camMove -= glm::vec3(0, 1, 0); }
			camMove *= camSpeed * deltaTime;
			_camera.Move(camMove);
		}

		Renderer::UpdateSceneView(_sceneView, int(imWindowSize.x), int(imWindowSize.y), _camera);
		ImGui::Image(UIManager::SceneView(_sceneView), imWindowSize);
	}

	ImGui::End();
}

void SceneWindow::Invalidate() {
	_camera.SetViewport(_windowSize.x, _windowSize.y);
}
}  // namespace Luna
