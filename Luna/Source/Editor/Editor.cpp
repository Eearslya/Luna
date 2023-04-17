#include <Luna/Assets/AssetManager.hpp>
#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Window.hpp>
#include <Luna/Editor/Editor.hpp>
#include <Luna/Platform/Filesystem.hpp>
#include <Luna/Platform/Windows/OSFilesystem.hpp>
#include <Luna/Project/Project.hpp>
#include <Luna/UI/UI.hpp>
#include <Tracy/Tracy.hpp>

namespace Luna {
static struct EditorState { ProjectHandle Project; } State;

static void CloseProject();
static void OpenProject(const Path& projectPath);
static void SaveProject();
static void UpdateProjectBrowser();

static void WindowContentBrowser();

bool Editor::Initialize() {
	ZoneScopedN("Editor::Initialize");

	Engine::GetMainWindow()->SetTitle("Luna Editor");

	return true;
}

void Editor::Update(double deltaTime) {
	if (!State.Project) {
		UpdateProjectBrowser();
		return;
	}

	UI::BeginDockspace(true);

	WindowContentBrowser();

	UI::EndDockspace();
}

void Editor::Shutdown() {
	ZoneScopedN("Editor::Shutdown");

	CloseProject();
}

void CloseProject() {
	if (!State.Project) { return; }

	SaveProject();
	AssetManager::Shutdown();
	Filesystem::UnregisterProtocol("project");
	Project::SetActive({});

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

void WindowContentBrowser() {
	if (ImGui::Begin("Content Browser")) {}
	ImGui::End();
}
}  // namespace Luna
