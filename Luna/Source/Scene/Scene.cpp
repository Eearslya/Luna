#include <imgui.h>

#include <Luna/Graphics/Graphics.hpp>
#include <Luna/Scene/Scene.hpp>
#include <Luna/Scene/TransformComponent.hpp>

namespace Luna {
Scene::Scene() {
	_root               = _registry.create();
	auto& rootTransform = _registry.emplace<TransformComponent>(_root);
	rootTransform.Name  = "Root";
}

Scene::~Scene() noexcept {}

entt::entity Scene::CreateEntity(const std::string& name, std::optional<entt::entity> parent) {
	entt::entity realParent = parent.has_value() ? parent.value() : _root;

	entt::entity e   = _registry.create();
	auto& transform  = _registry.emplace<TransformComponent>(e);
	transform.Parent = realParent;
	transform.Name   = name;

	if (_registry.valid(realParent)) {
		auto& parentTransform = _registry.get<TransformComponent>(realParent);
		parentTransform.Children.push_back(e);
	}

	return e;
}

void Scene::DrawSceneGraph() {
	if (ImGui::Begin("Scene")) {
		if (ImGui::BeginTable("SceneGraph", 2, ImGuiTableFlags_BordersInnerV)) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::BeginGroup();
			std::function<void(const entt::entity)> DisplayEntity = [&](const entt::entity entity) -> void {
				const auto& transform  = _registry.get<TransformComponent>(entity);
				const bool hasChildren = transform.Children.size() > 0;
				if (hasChildren) {
					bool open = ImGui::TreeNodeEx(transform.Name.c_str());
					if (open) {
						for (const auto child : transform.Children) { DisplayEntity(child); }
						ImGui::TreePop();
					}
				} else {
					ImGui::TreeNodeEx(transform.Name.c_str(),
					                  ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_NoTreePushOnOpen |
					                    ImGuiTreeNodeFlags_SpanFullWidth);
				}
			};
			DisplayEntity(_root);
			ImGui::EndGroup();

			ImGui::TableNextColumn();
			ImGui::Text("Components");
			ImGui::Text("Components");
			ImGui::Text("Components");
			ImGui::Text("Components");
			ImGui::Text("Components");
			ImGui::Text("Components");
			ImGui::Text("Components");
			ImGui::Text("Components");
			ImGui::Text("Components");
			ImGui::Text("Components");
			ImGui::Text("Components");
			ImGui::Text("Components");
			ImGui::Text("Components");
			ImGui::Text("Components");
			ImGui::Text("Components");
			ImGui::Text("Components");
			ImGui::Text("Components");
			ImGui::Text("Components");
			ImGui::Text("Components");
			ImGui::Text("Components");
			ImGui::Text("Components");
			ImGui::Text("Components");

			ImGui::EndTable();
		}

		ImGui::End();
	}
}
}  // namespace Luna
