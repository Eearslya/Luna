#include <imgui.h>
//
#include <ImGuizmo.h>
#include <stb_image.h>
#include <tiny_gltf.h>

#include <Luna/Core/Log.hpp>
#include <Luna/Filesystem/Filesystem.hpp>
#include <Luna/Graphics/AssetManager.hpp>
#include <Luna/Graphics/Graphics.hpp>
#include <Luna/Scene/MeshRenderer.hpp>
#include <Luna/Scene/Scene.hpp>
#include <Luna/Scene/TransformComponent.hpp>
#include <Luna/Scene/WorldData.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/Sampler.hpp>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

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

void Scene::LoadEnvironment(const std::string& filePath) {
	auto graphics = Graphics::Get();
	graphics->GetAssetManager().LoadEnvironment(filePath, *this);
}

entt::entity Scene::LoadModel(const std::string& filePath, entt::entity parent) {
	const std::filesystem::path gltfPath(filePath);
	const auto gltfFileName = gltfPath.filename().string();

	auto graphics   = Graphics::Get();
	auto rootEntity = CreateEntity(gltfFileName, parent);
	graphics->GetAssetManager().LoadModel(filePath, *this, rootEntity);

	return rootEntity;
}

void Scene::DrawSceneGraph() {
	if (ImGui::Begin("Heirarchy")) {
		std::function<void(const entt::entity)> DisplayEntity = [&](const entt::entity entity) -> void {
			const auto& transform       = _registry.get<TransformComponent>(entity);
			const bool hasChildren      = transform.Children.size() > 0;
			ImGuiTreeNodeFlags addFlags = entity == _selected ? ImGuiTreeNodeFlags_Selected : 0;
			if (hasChildren) {
				bool open = ImGui::TreeNodeEx(transform.Name.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | addFlags);
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { _selected = entity; }
				if (open) {
					for (const auto child : transform.Children) { DisplayEntity(child); }
					ImGui::TreePop();
				}
			} else {
				ImGui::TreeNodeEx(
					transform.Name.c_str(),
					ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_NoTreePushOnOpen | addFlags);
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { _selected = entity; }
			}
		};
		DisplayEntity(_root);
	}
	ImGui::End();

	if (ImGui::Begin("Inspector")) {
		if (_registry.valid(_selected)) {
			if (auto* comp = _registry.try_get<TransformComponent>(_selected)) { comp->DrawComponent(_registry); }
			if (auto* comp = _registry.try_get<WorldData>(_selected)) { comp->DrawComponent(_registry); }
			if (auto* comp = _registry.try_get<MeshRenderer>(_selected)) { comp->DrawComponent(_registry); }
		}
	}
	ImGui::End();
}
}  // namespace Luna
