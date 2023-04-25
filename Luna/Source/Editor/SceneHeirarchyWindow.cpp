#include <imgui_internal.h>

#include <Luna/Assets/AssetManager.hpp>
#include <Luna/Assets/Mesh.hpp>
#include <Luna/Editor/Editor.hpp>
#include <Luna/Editor/EditorContent.hpp>
#include <Luna/Editor/SceneHeirarchyWindow.hpp>
#include <Luna/Scene/MeshRendererComponent.hpp>
#include <Luna/Scene/NameComponent.hpp>
#include <Luna/Scene/RelationshipComponent.hpp>
#include <Luna/Scene/Scene.hpp>
#include <Luna/Scene/TransformComponent.hpp>
#include <Luna/UI/UI.hpp>
#include <Luna/Utility/Log.hpp>
#include <functional>
#include <optional>

namespace Luna {
void SceneHeirarchyWindow::OnProjectChanged() {}

template <typename T, typename... Args>
static bool AddComponentMenu(Entity entity, const char* label, Args&&... args) {
	const bool showItem = !entity.HasComponent<T>();
	if (showItem && ImGui::MenuItem(label)) { entity.AddComponent<T>(std::forward<Args>(args)...); }

	return showItem;
}

void SceneHeirarchyWindow::Update(double deltaTime) {
	if (ImGui::Begin("Heirarchy##SceneHeirarchyWindow")) {
		auto& scene = Editor::GetActiveScene();
		if (!_selected) { _selected = {}; }

		auto rootEntities = scene.GetRootEntities();
		for (auto entity : rootEntities) { DrawEntity(entity); }

		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() && ImGui::IsWindowFocused()) {
			_selected = {};
		}

		if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
			if (ImGui::MenuItem("Create Entity")) { _selected = scene.CreateEntity(); }

			ImGui::EndPopup();
		}
	}
	ImGui::End();

	ImGui::SetNextWindowSizeConstraints(ImVec2(350.0f, -1.0f), ImVec2(std::numeric_limits<float>::infinity(), -1.0f));
	if (ImGui::Begin("Inspector##SceneInspectorWindow") && _selected) {
		DrawComponents(_selected);
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Button("Add Component");
		if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonLeft)) {
			bool anyShown = false;

			anyShown |= AddComponentMenu<MeshRendererComponent>(_selected, ICON_FA_CIRCLE_NODES " MeshRenderer");

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

template <typename T>
static bool AssetButton(const char* id, AssetHandle& handle, AssetMetadata& metadata) {
	bool clear             = false;
	AssetMetadata newAsset = {};

	ImGui::PushID(id);

	metadata                = AssetManager::GetAssetMetadata(handle);
	std::string fileDisplay = "None";
	std::string pathDisplay = fmt::format("No {} loaded", AssetTypeToString(T::GetAssetType()));
	if (metadata.IsValid()) {
		fileDisplay = metadata.FilePath.Filename();
		pathDisplay = metadata.FilePath.String();
	} else {
		clear = true;
	}
	if (fileDisplay.size() > 17) { fileDisplay = fmt::format("{}...", fileDisplay.substr(0, 17)); }

	const char* assetIcon = ICON_FA_FILE;
	switch (T::GetAssetType()) {
		case AssetType::Mesh:
			assetIcon = ICON_FA_CIRCLE_NODES;
			break;

		default:
			break;
	}
	const std::string buttonText = fmt::format("{} {}", assetIcon, fileDisplay);
	ImGui::Button(buttonText.c_str());
	if (ImGui::BeginDragDropTarget()) {
		const ImGuiPayload* payload  = ImGui::GetDragDropPayload();
		const EditorContent* content = static_cast<const EditorContent*>(payload->Data);

		const auto& metadata = AssetManager::GetAssetMetadata(content->ContentPath);
		if (metadata.IsValid() && metadata.Type == T::GetAssetType()) {
			if (ImGui::AcceptDragDropPayload("EditorContent")) { newAsset = metadata; }
		}

		ImGui::EndDragDropTarget();
	}
	if (ImGui::IsItemHovered()) {
		ImGui::BeginTooltip();
		ImGui::Text("%s", pathDisplay.c_str());
		ImGui::EndTooltip();
	}
	if (metadata.IsValid()) {
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
		if (ImGui::Button(ICON_FA_XMARK)) { clear = true; }
		ImGui::PopStyleColor();
	}

	if (clear) {
		handle   = AssetHandle(0);
		metadata = {};
	}
	if (newAsset.IsValid()) {
		handle   = newAsset.Handle;
		metadata = newAsset;
	}

	ImGui::PopID();

	return metadata.IsValid();
}

template <typename T>
static bool AssetButton(const char* id, AssetHandle& handle) {
	AssetMetadata metadata;

	return AssetButton(id, handle, metadata);
}

template <typename T>
static bool DefaultPropertyMenu(Entity entity, T& component) {
	bool deleted = false;
	if (ImGui::MenuItem(ICON_FA_TRASH_CAN " Remove Component")) { deleted = true; }

	return deleted;
}

template <typename T>
static void DrawComponent(Entity& entity,
                          const std::string& label,
                          std::function<bool(Entity, T&)> drawFn,
                          std::optional<std::function<bool(Entity, T&)>> propsFn = std::nullopt) {
	bool hasPropertyMenu                       = propsFn.has_value();
	bool propertyMenu                          = false;
	bool deleted                               = false;
	std::function<bool(Entity, T&)> properties = hasPropertyMenu ? propsFn.value() : DefaultPropertyMenu<T>;

	if (entity.HasComponent<T>()) {
		const std::string compId = label + "##Properties";
		ImGui::PushID(compId.c_str());
		if (UI::CollapsingHeader(label.c_str(), &propertyMenu, ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_WRENCH)) {
			auto& component = entity.GetComponent<T>();
			deleted |= drawFn(entity, component);

			if (propertyMenu) { ImGui::OpenPopup(compId.c_str()); }

			if (ImGui::BeginPopup(compId.c_str())) {
				deleted |= properties(entity, component);
				ImGui::EndPopup();
			}
		}

		if (deleted) { entity.RemoveComponent<T>(); }

		ImGui::PopID();
	}
}

void SceneHeirarchyWindow::DrawComponents(Entity& entity) {
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

	auto& cName = entity.GetComponent<NameComponent>();
	char nameBuffer[256];
	strncpy_s(nameBuffer, cName.Name.data(), 256);

	if (ImGui::BeginTable("NameComponent_Properties", 2, ImGuiTableFlags_BordersInnerV)) {
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed, 85.0f);
		ImGui::TableNextColumn();
		ImGui::Text("Name");
		ImGui::TableNextColumn();
		if (ImGui::InputText("##Name", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
			if (strlen(nameBuffer) > 0) { cName.Name = nameBuffer; }
		}

		ImGui::EndTable();
	}

	ImGui::PopStyleVar();
	ImGui::Spacing();
	ImGui::Separator();

	DrawComponent<TransformComponent>(
		entity,
		ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " Transform",
		[](Entity entity, auto& cTransform) -> bool {
			const auto EditVec3 = [](const std::string& label,
		                           glm::vec3& value,
		                           float speed      = 0.1f,
		                           float resetValue = 0.0f,
		                           bool* lock       = nullptr) {
				auto& io    = ImGui::GetIO();
				auto& style = ImGui::GetStyle();

				const float lineHeight = io.Fonts->Fonts[0]->FontSize + style.FramePadding.y * 2.0f;
				const ImVec2 buttonSize(lineHeight + 3.0f, lineHeight);
				const bool locked = lock && *lock;

				ImGui::TableNextColumn();
				ImGui::Text("%s", label.c_str());

				ImGui::TableNextColumn();
				ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
				ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
				ImGui::PushID(label.c_str());

				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.61f, 0.006f, 0.015f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.79f, 0.03f, 0.03f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_Button]);
				ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
				if (ImGui::Button("X", buttonSize)) {
					if (locked) {
						value = glm::vec3(resetValue);
					} else {
						value.x = resetValue;
					}
				}
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					value = glm::vec3(resetValue);
				}
				ImGui::PopStyleColor();
				ImGui::SameLine();
				if (ImGui::DragFloat("##XValue", &value.x, speed, 0.0f, 0.0f, "%.2f")) {
					if (locked) { value = glm::vec3(value.x); }
				}
				ImGui::PopItemWidth();
				ImGui::SameLine();
				ImGui::PopStyleColor(3);

				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.03f, 0.45f, 0.03f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.1f, 0.55f, 0.1f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_Button]);
				ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
				if (ImGui::Button("Y", buttonSize)) {
					if (locked) {
						value = glm::vec3(resetValue);
					} else {
						value.y = resetValue;
					}
				}
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					value = glm::vec3(resetValue);
				}
				ImGui::PopStyleColor();
				ImGui::SameLine();
				if (ImGui::DragFloat("##YValue", &value.y, speed, 0.0f, 0.0f, "%.2f")) {
					if (locked) { value = glm::vec3(value.y); }
				}
				ImGui::PopItemWidth();
				ImGui::SameLine();
				ImGui::PopStyleColor(3);

				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.006f, 0.25f, 0.61f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.03f, 0.35f, 0.79f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_Button]);
				ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
				if (ImGui::Button("Z", buttonSize)) {
					if (locked) {
						value = glm::vec3(resetValue);
					} else {
						value.z = resetValue;
					}
				}
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					value = glm::vec3(resetValue);
				}
				ImGui::PopStyleColor();
				ImGui::SameLine();
				if (ImGui::DragFloat("##ZValue", &value.z, speed, 0.0f, 0.0f, "%.2f")) {
					if (locked) { value = glm::vec3(value.z); }
				}
				ImGui::PopItemWidth();
				ImGui::PopStyleColor(3);

				if (lock) {
					ImGui::SameLine();
					ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
					if (ImGui::Button(*lock ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN)) { *lock = !*lock; }
					ImGui::PopStyleColor();
				}

				ImGui::PopID();
				ImGui::PopStyleVar(2);
			};

			if (ImGui::BeginTable("TransformComponent_Properties", 2, ImGuiTableFlags_BordersInnerV)) {
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed, 75.0f);
				ImGui::TableNextRow();
				EditVec3("Translation", cTransform.Translation, 0.1f);
				ImGui::TableNextRow();
				EditVec3("Rotation", cTransform.Rotation, 0.5f);
				ImGui::TableNextRow();
				EditVec3("Scale", cTransform.Scale, 0.1f, 1.0f, &cTransform.LockScale);

				ImGui::EndTable();
			}

			return false;
		},
		[](Entity entity, auto& cTransform) -> bool {
			if (ImGui::MenuItem(ICON_FA_ARROW_ROTATE_LEFT " Reset to Identity")) {
				cTransform.Translation = glm::vec3(0.0f);
				cTransform.Rotation    = glm::vec3(0.0f);
				cTransform.Scale       = glm::vec3(1.0f);
			}

			return false;
		});

	DrawComponent<MeshRendererComponent>(
		entity, ICON_FA_CIRCLE_NODES " Mesh Renderer", [](Entity entity, auto& cMeshRenderer) -> bool {
			if (ImGui::BeginTable("MeshRendererComponent_Properties", 2, ImGuiTableFlags_BordersInnerV)) {
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed, 85.0f);

				ImGui::TableNextColumn();
				ImGui::Text("Mesh");
				ImGui::TableNextColumn();
				AssetMetadata meshMeta;
				if (AssetButton<Mesh>("Mesh", cMeshRenderer.MeshAsset, meshMeta)) {}

				ImGui::EndTable();
			}

			return false;
		});
}

void SceneHeirarchyWindow::DrawEntity(Entity& entity) {
	auto& scene = Editor::GetActiveScene();

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (entity == _selected) { flags |= ImGuiTreeNodeFlags_Selected; }

	const entt::entity entityId = entity;
	const void* nodeId          = reinterpret_cast<void*>(uint64_t(entityId));
	bool deleted                = false;

	const auto& cName         = entity.GetComponent<NameComponent>();
	const auto& cRelationship = entity.GetComponent<RelationshipComponent>();

	const bool hasChildren = cRelationship.FirstChild != entt::null;
	if (!hasChildren) { flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet; }

	const bool open = ImGui::TreeNodeEx(nodeId, flags, "%s", cName.Name.c_str());
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) { _selected = entity; }

	if (ImGui::BeginPopupContextItem()) {
		if (ImGui::MenuItem("Delete")) { deleted = true; }

		ImGui::EndPopup();
	}

	if (open) {
		if (hasChildren) {
			auto children = entity.GetChildren();
			for (auto child : children) { DrawEntity(child); }
		}

		ImGui::TreePop();
	}

	if (deleted) { scene.DestroyEntity(entity); }
}
}  // namespace Luna
