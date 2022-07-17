#include "SceneHierarchyPanel.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <optional>

#include "../ImGuiRenderer.hpp"
#include "CameraComponent.hpp"
#include "Entity.hpp"
#include "MeshComponent.hpp"
#include "NameComponent.hpp"
#include "RelationshipComponent.hpp"
#include "Scene.hpp"
#include "TransformComponent.hpp"

namespace Luna {
SceneHierarchyPanel::SceneHierarchyPanel(const std::shared_ptr<Scene>& scene) : _scene(scene) {}

template <typename T, typename... Args>
static bool AddComponentMenu(Entity entity, const char* label, Args&&... args) {
	const bool showItem = !entity.HasComponent<T>();
	if (showItem && ImGui::MenuItem(label)) { entity.AddComponent<T>(std::forward<Args>(args)...); }

	return showItem;
}

void SceneHierarchyPanel::Render() {
	auto scene = _scene.lock();
	if (!scene) {
		_scene.reset();
		return;
	}

	auto& registry = scene->_registry;

	if (ImGui::Begin("Hierarchy")) {
		if (!_selected) { _selected = {}; }

		registry.each([&](auto entityId) {
			Entity entity(entityId, *scene);
			DrawEntity(entity);
		});

		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() && ImGui::IsWindowFocused()) {
			_selected = {};
		}

		if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight, false)) {
			if (ImGui::MenuItem(ICON_FA_PLUS " Create Entity")) { _selected = scene->CreateEntity(); }

			ImGui::EndPopup();
		}
	}
	ImGui::End();

	ImGui::SetNextWindowSizeConstraints(ImVec2(350.0f, -1.0f), ImVec2(std::numeric_limits<float>::infinity(), -1.0f));
	if (ImGui::Begin("Properties") && _selected) {
		DrawComponents(_selected);
		ImGui::Separator();
		ImGui::Button(ICON_FA_PLUS " Add Component");
		if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonLeft)) {
			bool anyShown = false;

			anyShown |= AddComponentMenu<CameraComponent>(_selected, ICON_FA_CAMERA " Camera");
			anyShown |= AddComponentMenu<MeshComponent>(_selected, ICON_FA_CIRCLE_NODES " Mesh");

			if (!anyShown) {
				ImGui::BeginDisabled();
				ImGui::MenuItem(ICON_FA_X " No Components Available");
				ImGui::EndDisabled();
			}

			ImGui::EndPopup();
		}
	}
	ImGui::End();
}

void SceneHierarchyPanel::DrawEntity(Entity entity) {
	auto scene = _scene.lock();
	if (!scene) {
		_scene.reset();
		return;
	}

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (entity == _selected) { flags |= ImGuiTreeNodeFlags_Selected; }

	const entt::entity entityId = entity;
	const void* nodeId          = reinterpret_cast<void*>(uint64_t(entityId));
	bool deleted                = false;

	const auto& cName         = entity.GetComponent<NameComponent>();
	const auto& cRelationship = entity.GetComponent<RelationshipComponent>();

	if (cRelationship.FirstChild == entt::null) { flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet; }

	const bool open = ImGui::TreeNodeEx(nodeId, flags, "%s", cName.Name.c_str());
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) { _selected = entity; }

	if (ImGui::BeginPopupContextItem()) {
		if (ImGui::MenuItem(ICON_FA_TRASH_CAN " Delete")) { deleted = true; }
		ImGui::EndPopup();
	}

	if (open) { ImGui::TreePop(); }

	if (deleted) { scene->DestroyEntity(entity); }
}

// Custom CollapsingHeader implementation that allows for a special button on the right side of the header.
static bool CollapsingHeader(const char* label, bool* specialClick, ImGuiTreeNodeFlags flags, const char* buttonLabel) {
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems) { return false; }
	const auto& style = ImGui::GetStyle();

	const ImVec2 buttonSize(GImGui->FontSize + style.FramePadding.x * 2.0f,
	                        GImGui->FontSize + style.FramePadding.y * 2.0f);

	flags |= ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_AllowItemOverlap |
	         static_cast<ImGuiTreeNodeFlags>(ImGuiTreeNodeFlags_ClipLabelForTrailingButton);

	ImGuiID id  = window->GetID(label);
	bool isOpen = ImGui::TreeNodeBehavior(id, flags, label);
	ImGui::SameLine(ImGui::GetItemRectSize().x - buttonSize.x);
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	if (ImGui::Button(buttonLabel, buttonSize)) { *specialClick = true; }
	ImGui::PopStyleColor();

	return isOpen;
}

template <typename T>
static void DrawComponent(Entity entity,
                          const std::string& label,
                          std::function<bool(Entity, T&)> drawFn,
                          std::optional<std::function<bool(Entity, T&)>> propsFn = std::nullopt) {
	bool hasPropertyMenu = propsFn.has_value();
	bool propertyMenu    = false;
	bool deleted         = false;

	if (entity.HasComponent<T>()) {
		const std::string compId = label + "##Properties";
		ImGui::PushID(compId.c_str());
		if (hasPropertyMenu &&
		    CollapsingHeader(
					label.c_str(), hasPropertyMenu ? &propertyMenu : nullptr, ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_WRENCH)) {
			auto& component = entity.GetComponent<T>();
			deleted |= drawFn(entity, component);

			if (propertyMenu) { ImGui::OpenPopup(compId.c_str()); }

			if (ImGui::BeginPopup(compId.c_str())) {
				deleted |= propsFn.value()(entity, component);
				ImGui::EndPopup();
			}
		} else if (ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
			auto& component = entity.GetComponent<T>();
			deleted |= drawFn(entity, component);
		}

		if (deleted) { entity.RemoveComponent<T>(); }

		ImGui::PopID();
	}
}

void SceneHierarchyPanel::DrawComponents(Entity entity) {
	// Name
	if (entity.HasComponent<NameComponent>()) {
		auto& cName = entity.GetComponent<NameComponent>();
		char nameBuffer[256];
		strcpy_s(nameBuffer, cName.Name.data());

		if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
			if (strlen(nameBuffer) > 0) { cName.Name = nameBuffer; }
		}

		ImGui::Separator();
	}

	// Transform
	DrawComponent<TransformComponent>(
		entity,
		ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " Transform",
		[](Entity entity, TransformComponent& cTransform) {
			const auto EditVec3 = [](const std::string& label,
		                           glm::vec3& value,
		                           float speed       = 0.1f,
		                           float resetValue  = 0.0f,
		                           float columnWidth = 100.0f) {
				auto& io    = ImGui::GetIO();
				auto& style = ImGui::GetStyle();

				const float lineHeight = io.Fonts->Fonts[0]->FontSize + style.FramePadding.y * 2.0f;
				const ImVec2 buttonSize(lineHeight + 3.0f, lineHeight);

				ImGui::Columns(2);
				ImGui::SetColumnWidth(0, columnWidth);

				ImGui::Text("%s", label.c_str());
				ImGui::NextColumn();

				ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
				ImGui::PushID(label.c_str());

				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.61f, 0.006f, 0.015f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.79f, 0.03f, 0.03f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_Button]);
				if (ImGui::Button("X", buttonSize)) { value.x = resetValue; }
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					value = glm::vec3(resetValue);
				}
				ImGui::SameLine();
				ImGui::DragFloat("##XValue", &value.x, speed, 0.0f, 0.0f, "%.2f");
				ImGui::PopItemWidth();
				ImGui::SameLine();
				ImGui::PopStyleColor(3);

				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.03f, 0.45f, 0.03f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.1f, 0.55f, 0.1f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_Button]);
				if (ImGui::Button("Y", buttonSize)) { value.y = resetValue; }
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					value = glm::vec3(resetValue);
				}
				ImGui::SameLine();
				ImGui::DragFloat("##YValue", &value.y, speed, 0.0f, 0.0f, "%.2f");
				ImGui::PopItemWidth();
				ImGui::SameLine();
				ImGui::PopStyleColor(3);

				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.006f, 0.25f, 0.61f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.03f, 0.35f, 0.79f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_Button]);
				if (ImGui::Button("Z", buttonSize)) { value.z = resetValue; }
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					value = glm::vec3(resetValue);
				}
				ImGui::SameLine();
				ImGui::DragFloat("##ZValue", &value.z, speed, 0.0f, 0.0f, "%.2f");
				ImGui::PopItemWidth();
				ImGui::PopStyleColor(3);

				ImGui::PopID();
				ImGui::PopStyleVar();

				ImGui::Columns(1);
			};

			EditVec3("Translation", cTransform.Translation, 0.1f);
			EditVec3("Rotation", cTransform.Rotation, 0.5f);
			EditVec3("Scale", cTransform.Scale, 0.1f, 1.0f);
			ImGui::Spacing();

			return false;
		},
		[](Entity entity, auto& cTransform) {
			if (ImGui::MenuItem(ICON_FA_ARROW_ROTATE_LEFT " Reset to Identity")) {
				cTransform.Translation = glm::vec3(0.0f);
				cTransform.Rotation    = glm::vec3(0.0f);
				cTransform.Scale       = glm::vec3(1.0f);
			}

			return false;
		});

	// Camera
	DrawComponent<CameraComponent>(
		entity,
		ICON_FA_CAMERA " Camera",
		[](Entity entity, auto& cCamera) {
			auto& camera     = cCamera.Camera;
			float fovDegrees = camera.GetFovDegrees();
			float zNear      = camera.GetZNear();
			float zFar       = camera.GetZFar();

			ImGui::Columns(2);
			ImGui::SetColumnWidth(0, 125.0f);

			ImGui::Text("Primary Camera");
			ImGui::NextColumn();
			ImGui::Checkbox("##PrimaryCamera", &cCamera.Primary);
			ImGui::NextColumn();

			ImGui::Text("Field of View");
			ImGui::NextColumn();
			ImGui::DragFloat("##FieldOfView", &fovDegrees, 0.5f, 30.0f, 90.0f, "%.1f deg");
			ImGui::NextColumn();

			ImGui::Text("Near Plane");
			ImGui::NextColumn();
			ImGui::DragFloat("##NearPlane", &zNear, 0.01f, 0.001f, 10.0f, "%.3f");
			ImGui::NextColumn();

			ImGui::Text("Far Plane");
			ImGui::NextColumn();
			ImGui::DragFloat("##FarPlane", &zFar, 1.0f, 1.0f, 100'000.0f, "%.2f");

			camera.SetPerspective(fovDegrees, zNear, zFar);

			ImGui::Columns(1);

			return false;
		},
		[](Entity entity, auto& cCamera) {
			bool deleted = false;
			if (ImGui::MenuItem(ICON_FA_TRASH_CAN " Remove Component")) { deleted = true; }

			return deleted;
		});

	// Mesh
	DrawComponent<MeshComponent>(
		entity,
		ICON_FA_CIRCLE_NODES " Mesh",
		[](Entity entity, auto& cMesh) { return false; },
		[](Entity entity, auto& cMesh) {
			bool deleted = false;
			if (ImGui::MenuItem(ICON_FA_TRASH_CAN " Remove Component")) { deleted = true; }

			return deleted;
		});
}
}  // namespace Luna
