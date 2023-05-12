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
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Luna {
SceneWindow::SceneWindow(int index) : _windowIndex(index), _windowSize(0.0f, 0.0f) {
	_gizmoMode   = ImGuizmo::TRANSLATE;
	_sceneView   = Renderer::RegisterSceneView();
	_windowTitle = "Scene##SceneWindow" + std::to_string(_windowIndex);

	Input::OnMouseMoved.Add(
		[this](const glm::dvec2& pos) {
			if (_cameraControl) {
				auto& camera            = Editor::GetActiveScene().GetEditorCamera();
				const float sensitivity = 0.5f;
				camera.Rotate(pos.y * sensitivity, -(pos.x * sensitivity));
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

		auto& camera = Editor::GetActiveScene().GetEditorCamera();
		camera.SetViewport(_windowSize.x, _windowSize.y);
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
			camera.Move(camMove);
		}

		Renderer::UpdateSceneView(_sceneView, int(imWindowSize.x), int(imWindowSize.y), camera);
		ImGui::Image(UIManager::SceneView(_sceneView), imWindowSize);

		auto gizmoButtons = ImGui::GetWindowContentRegionMin();
		gizmoButtons.x += 8.0f;
		gizmoButtons.y += 8.0f;
		ImGui::SetCursorPos(gizmoButtons);

		UI::BeginButtonGroup(3);
		if (UI::GroupedButton(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, _gizmoMode == ImGuizmo::TRANSLATE)) {
			_gizmoMode = ImGuizmo::TRANSLATE;
		}
		if (UI::GroupedButton(ICON_FA_ARROW_ROTATE_RIGHT, _gizmoMode == ImGuizmo::ROTATE)) {
			_gizmoMode = ImGuizmo::ROTATE;
		}
		if (UI::GroupedButton(ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, _gizmoMode == ImGuizmo::SCALE)) {
			_gizmoMode = ImGuizmo::SCALE;
		}
		UI::EndButtonGroup();

		auto selected = Editor::GetSelectedEntity();
		if (selected) {
			const auto windowPos = ImGui::GetWindowPos();
			auto windowMax       = ImGui::GetWindowContentRegionMax();
			auto windowMin       = ImGui::GetWindowContentRegionMin();
			windowMax.x += windowPos.x;
			windowMax.y += windowPos.y;
			windowMin.x += windowPos.x;
			windowMin.y += windowPos.y;
			const ImVec2 windowSize(windowMax.x - windowMin.x, windowMax.y - windowMin.y);

			auto transform  = selected.GetGlobalTransform();
			glm::mat4 delta = glm::mat4(1.0f);
			const auto view = camera.GetView();
			auto proj       = camera.GetProjection();
			proj[0].y *= -1.0f;
			proj[1].y *= -1.0f;
			proj[2].y *= -1.0f;
			proj[3].y *= -1.0f;
			ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
			ImGuizmo::SetRect(windowMin.x, windowMin.y, windowSize.x, windowSize.y);
			if (ImGuizmo::Manipulate(glm::value_ptr(view),
			                         glm::value_ptr(proj),
			                         ImGuizmo::OPERATION(_gizmoMode),
			                         ImGuizmo::LOCAL,
			                         glm::value_ptr(transform),
			                         glm::value_ptr(delta),
			                         nullptr,
			                         nullptr,
			                         nullptr)) {
				glm::vec3 translate;
				glm::quat rotate;
				glm::vec3 scale;
				glm::vec3 skew;
				glm::vec4 perspective;
				glm::decompose(delta, scale, rotate, translate, skew, perspective);

				if (_gizmoMode == ImGuizmo::TRANSLATE) {
					selected.Translate(translate);
				} else if (_gizmoMode == ImGuizmo::ROTATE) {
					selected.Rotate(glm::eulerAngles(rotate));
				} else if (_gizmoMode == ImGuizmo::SCALE) {
					selected.Scale(scale);
				}
			}
		}
	}

	ImGui::End();
}

void SceneWindow::Invalidate() {
	auto& camera = Editor::GetActiveScene().GetEditorCamera();
	camera.SetViewport(_windowSize.x, _windowSize.y);
}
}  // namespace Luna
