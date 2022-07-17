#include "SceneHeirarchyPanel.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <optional>

#include "../ImGuiRenderer.hpp"
#include "Entity.hpp"
#include "NameComponent.hpp"
#include "RelationshipComponent.hpp"
#include "Scene.hpp"
#include "TransformComponent.hpp"

namespace Luna {
SceneHeirarchyPanel::SceneHeirarchyPanel(const std::shared_ptr<Scene>& scene) : _scene(scene) {}

void SceneHeirarchyPanel::Render() {
	auto scene = _scene.lock();
	if (!scene) {
		_scene.reset();
		return;
	}

	auto& registry = scene->_registry;

	if (ImGui::Begin("Heirarchy")) {
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
		if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonLeft)) { ImGui::EndPopup(); }
	}
	ImGui::End();
}

void SceneHeirarchyPanel::DrawEntity(Entity entity) {
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
	ImGui::SameLine();
	ImGui::SetCursorPosX(ImGui::GetItemRectSize().x - buttonSize.x);
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	if (ImGui::Button(buttonLabel, buttonSize)) { *specialClick = true; }
	ImGui::PopStyleColor();

	return isOpen;
}

template <typename T>
static void DrawComponent(Entity entity,
                          const std::string& label,
                          std::function<void(Entity, T&)> drawFn,
                          std::optional<std::function<void(Entity, T&)>> propsFn = std::nullopt) {
	bool hasPropertyMenu = propsFn.has_value();
	bool propertyMenu    = false;

	if (entity.HasComponent<T>()) {
		if (hasPropertyMenu &&
		    CollapsingHeader(
					label.c_str(), hasPropertyMenu ? &propertyMenu : nullptr, ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_WRENCH)) {
			const std::string popupId = label + "##Properties";
			auto& component           = entity.GetComponent<T>();
			drawFn(entity, component);

			if (propertyMenu) { ImGui::OpenPopup(popupId.c_str()); }

			if (ImGui::BeginPopup(popupId.c_str())) {
				propsFn.value()(entity, component);
				ImGui::EndPopup();
			}
		} else if (ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
			auto& component = entity.GetComponent<T>();
			drawFn(entity, component);
		}
	}
}

void SceneHeirarchyPanel::DrawComponents(Entity entity) {
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
		entity, ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " Transform", [](Entity entity, TransformComponent& cTransform) {
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
		});
}
}  // namespace Luna
