#include <stb_image.h>

#include <Luna/Assets/AssetManager.hpp>
#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Window.hpp>
#include <Luna/Editor/AssetRegistryWindow.hpp>
#include <Luna/Editor/ContentBrowserWindow.hpp>
#include <Luna/Editor/Editor.hpp>
#include <Luna/Editor/EditorAssets.hpp>
#include <Luna/Editor/EditorWindow.hpp>
#include <Luna/Editor/MeshImportWindow.hpp>
#include <Luna/Editor/SceneHeirarchyWindow.hpp>
#include <Luna/Editor/SceneWindow.hpp>
#include <Luna/Platform/Filesystem.hpp>
#include <Luna/Platform/Windows/OSFilesystem.hpp>
#include <Luna/Project/Project.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Scene/MeshRendererComponent.hpp>
#include <Luna/Scene/Scene.hpp>
#include <Luna/UI/UI.hpp>
#include <Luna/UI/UIManager.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Tracy/Tracy.hpp>

namespace Luna {
static struct EditorState {
	ProjectHandle Project;
	std::vector<std::unique_ptr<EditorWindow>> Windows;
	std::vector<std::unique_ptr<EditorWindow>> NewWindows;
	std::unique_ptr<SceneWindow> SceneWindow;
	IntrusivePtr<Scene> Scene;
	AssetHandle SceneHandle;
	bool DemoWindow = false;
} State;

static void CloseProject();
static void OpenProject(const Path& projectPath);
static void SaveProject();
static void SaveScene(bool saveAs = false);
static void UpdateProjectBrowser();
static void UpdateTitle();

bool Editor::Initialize() {
	ZoneScopedN("Editor::Initialize");

	auto LoadImage = [](const Path& imagePath) -> Vulkan::ImageHandle {
		auto file = Filesystem::OpenReadOnlyMapping(imagePath);
		if (!file) { throw std::runtime_error("Failed to load Editor image!"); }

		int width, height, comp;
		stbi_uc* pixels = stbi_load_from_memory(file->Data<stbi_uc>(), file->GetSize(), &width, &height, &comp, 4);
		if (!pixels) { throw std::runtime_error("Failed to parse Editor image!"); }

		const Vulkan::ImageCreateInfo imageCI =
			Vulkan::ImageCreateInfo::Immutable2D(vk::Format::eR8G8B8A8Unorm, width, height, false);
		const Vulkan::ImageInitialData data{.Data = pixels};

		return Renderer::GetDevice().CreateImage(imageCI, &data);
	};
	EditorAssets::FileIcon      = LoadImage("res://Textures/File.png");
	EditorAssets::DirectoryIcon = LoadImage("res://Textures/Directory.png");

	Engine::GetMainWindow()->SetTitle("Luna Editor");

	State.Windows.emplace_back(new ContentBrowserWindow);
	State.Windows.emplace_back(new SceneHeirarchyWindow);
	State.SceneWindow.reset(new SceneWindow(0));

	return true;
}

void Editor::Update(double deltaTime) {
	if (!State.Project) {
		UpdateProjectBrowser();
		return;
	}

	UI::BeginDockspace(true);

	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK " Save Scene")) { SaveScene(); }
			if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK " Save Scene As...")) { SaveScene(true); }
			if (ImGui::MenuItem(ICON_FA_POWER_OFF " Exit")) { Engine::RequestShutdown(); }

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Window")) {
			if (ImGui::MenuItem("Asset Registry")) { State.NewWindows.emplace_back(new AssetRegistryWindow); }
			if (ImGui::MenuItem("ImGui Demo Window", nullptr, State.DemoWindow)) { State.DemoWindow = !State.DemoWindow; }

			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}

	if (State.SceneWindow) { State.SceneWindow->Update(deltaTime); }

	for (auto& window : State.NewWindows) { State.Windows.push_back(std::move(window)); }
	State.NewWindows.clear();

	State.Windows.erase(
		std::remove_if(State.Windows.begin(), State.Windows.end(), [](auto& windowPtr) { return windowPtr->Closed(); }),
		State.Windows.end());
	for (auto& window : State.Windows) { window->Update(deltaTime); }

	if (State.DemoWindow) { ImGui::ShowDemoWindow(&State.DemoWindow); }

	UI::EndDockspace();
}

void Editor::Shutdown() {
	ZoneScopedN("Editor::Shutdown");

	CloseProject();

	EditorAssets::DirectoryIcon.Reset();
	EditorAssets::FileIcon.Reset();
}

Scene& Editor::GetActiveScene() {
	return *State.Scene;
}

void Editor::RequestAsset(const Path& assetPath) {
	const auto extension = assetPath.Extension();

	if (extension == "gltf" || extension == "glb") {
		State.NewWindows.emplace_back(new MeshImportWindow(assetPath));
	} else if (extension == "lmesh") {
		const auto& metadata = AssetManager::GetAssetMetadata(assetPath);
		if (metadata.IsValid()) {
			auto entity             = GetActiveScene().CreateEntity(assetPath.Stem());
			auto& cMeshRenderer     = entity.AddComponent<MeshRendererComponent>();
			cMeshRenderer.MeshAsset = metadata.Handle;
		}
	} else if (extension == "lscene") {
		const auto& metadata = AssetManager::GetAssetMetadata(assetPath);
		if (metadata.IsValid()) {
			IntrusivePtr<Scene> scene = AssetManager::GetAsset<Scene>(metadata.Handle);
			if (scene) {
				State.Scene       = std::move(scene);
				State.SceneHandle = metadata.Handle;
				UpdateTitle();
			}
		}
	}
}

void CloseProject() {
	if (!State.Project) { return; }

	SaveProject();
	AssetManager::Shutdown();
	Filesystem::UnregisterProtocol("project");
	Project::SetActive({});

	for (auto& window : State.Windows) { window->OnProjectChanged(); }

	UpdateTitle();
}

void OpenProject(const Path& projectPath) {
	State.Project = MakeHandle<Project>(projectPath);

	// TODO: Temporary until project creation is implemented.
	if (!State.Project->Load()) { State.Project->Save(); }

	Filesystem::RegisterProtocol(
		"project", std::unique_ptr<FilesystemBackend>(new OSFilesystem(projectPath.BaseDirectory().WithoutProtocol())));

	Project::SetActive(State.Project);
	AssetManager::Initialize();

	State.Scene       = MakeHandle<Scene>();
	State.SceneHandle = 0;

	UpdateTitle();

	for (auto& window : State.Windows) { window->OnProjectChanged(); }
}

void SaveProject() {
	if (!State.Project) { return; }

	if (AssetManager::GetAssetMetadata(State.SceneHandle).IsValid()) { SaveScene(); }
	State.Project->Save();
}

void SaveScene(bool saveAs) {
	AssetMetadata sceneMeta = AssetManager::GetAssetMetadata(State.SceneHandle);
	if (!saveAs && sceneMeta.IsValid()) {
		AssetManager::SaveLoaded();
		AssetManager::SaveAsset(sceneMeta, State.Scene);
	} else {
		UIManager::TextDialog(
			"Save Scene As...",
			[](bool saved, const std::string& sceneName) {
				if (!saved) { return; }

				if (sceneName.empty()) {
					UIManager::Alert("Cannot Save Scene", "Cannot save a scene with an empty name!");
					return;
				}

				const Path assetPath = "Assets/Scenes/" + sceneName + ".lscene";
				auto scene           = AssetManager::CreateAsset<Scene>(assetPath);
				*scene               = std::move(*State.Scene);
				scene->SetName(sceneName);
				AssetManager::UnloadAsset(AssetManager::GetAssetMetadata(State.SceneHandle));
				State.Scene       = scene;
				State.SceneHandle = scene->Handle;
				AssetManager::SaveAsset(AssetManager::GetAssetMetadata(scene->Handle), scene);

				UpdateTitle();
			},
			State.Scene->GetName());
	}
}

void UpdateProjectBrowser() {
	UI::BeginDockspace();

	// TODO: Project browser/create menus.
	OpenProject("file://Project/Project.luna");

	UI::EndDockspace();
}

void UpdateTitle() {
	if (State.Project) {
		Engine::GetMainWindow()->SetTitle("Luna Editor - " + State.Project->GetSettings().Name + " (" +
		                                  State.Scene->GetName() + ")");
	} else {
		Engine::GetMainWindow()->SetTitle("Luna Editor");
	}
}
}  // namespace Luna
