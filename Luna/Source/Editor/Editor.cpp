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
#include <Luna/Platform/Filesystem.hpp>
#include <Luna/Platform/Windows/OSFilesystem.hpp>
#include <Luna/Project/Project.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/UI/UI.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Tracy/Tracy.hpp>

namespace Luna {
static struct EditorState {
	ProjectHandle Project;
	std::vector<std::unique_ptr<EditorWindow>> Windows;
	std::vector<std::unique_ptr<EditorWindow>> NewWindows;
} State;

static void CloseProject();
static void OpenProject(const Path& projectPath);
static void SaveProject();
static void UpdateProjectBrowser();

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

	return true;
}

void Editor::Update(double deltaTime) {
	if (!State.Project) {
		UpdateProjectBrowser();
		return;
	}

	UI::BeginDockspace(true);

	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("Window")) {
			if (ImGui::MenuItem("Asset Registry")) { State.NewWindows.emplace_back(new AssetRegistryWindow); }

			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}

	for (auto& window : State.NewWindows) { State.Windows.push_back(std::move(window)); }
	State.NewWindows.clear();

	State.Windows.erase(
		std::remove_if(State.Windows.begin(), State.Windows.end(), [](auto& windowPtr) { return windowPtr->Closed(); }),
		State.Windows.end());
	for (auto& window : State.Windows) { window->Update(deltaTime); }

	ImGui::ShowDemoWindow();

	UI::EndDockspace();
}

void Editor::Shutdown() {
	ZoneScopedN("Editor::Shutdown");

	CloseProject();

	EditorAssets::DirectoryIcon.Reset();
	EditorAssets::FileIcon.Reset();
}

void Editor::RequestAsset(const Path& assetPath) {
	const auto extension = assetPath.Extension();

	if (extension == "gltf" || extension == "glb") { State.NewWindows.emplace_back(new MeshImportWindow(assetPath)); }
}

void CloseProject() {
	if (!State.Project) { return; }

	SaveProject();
	AssetManager::Shutdown();
	Filesystem::UnregisterProtocol("project");
	Project::SetActive({});

	for (auto& window : State.Windows) { window->OnProjectChanged(); }

	Engine::GetMainWindow()->SetTitle("Luna Editor");
}

void OpenProject(const Path& projectPath) {
	State.Project = MakeHandle<Project>(projectPath);

	// TODO: Temporary until project creation is implemented.
	if (!State.Project->Load()) { State.Project->Save(); }

	Engine::GetMainWindow()->SetTitle("Luna Editor - " + State.Project->GetSettings().Name);
	Project::SetActive(State.Project);

	Filesystem::RegisterProtocol(
		"project", std::unique_ptr<FilesystemBackend>(new OSFilesystem(projectPath.BaseDirectory().WithoutProtocol())));
	AssetManager::Initialize();

	for (auto& window : State.Windows) { window->OnProjectChanged(); }
}

void SaveProject() {
	if (!State.Project) { return; }

	State.Project->Save();
}

void UpdateProjectBrowser() {
	UI::BeginDockspace();

	// TODO: Project browser/create menus.
	OpenProject("file://Project/Project.luna");

	UI::EndDockspace();
}
}  // namespace Luna
