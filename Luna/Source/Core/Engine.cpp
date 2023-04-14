#include <imgui.h>

#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Input.hpp>
#include <Luna/Core/Window.hpp>
#include <Luna/Core/WindowManager.hpp>
#include <Luna/Platform/Filesystem.hpp>
#include <Luna/Platform/Windows/OSFilesystem.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Renderer/ShaderManager.hpp>
#include <Luna/Scene/Scene.hpp>
#include <Luna/UI/UIManager.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Utility/Threading.hpp>
#include <Tracy/Tracy.hpp>

namespace Luna {
static struct EngineState {
	bool Initialized = false;
	bool Running     = false;
	std::unique_ptr<Window> Window;

	Scene ActiveScene;

	uint64_t FrameCount = 0;
	double LastFrame    = 0.0;
} State;

static void RenderMainUI(double deltaTime) {
	ImGuiIO& io = ImGui::GetIO();

	// Start Dockspace
	{
		ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
		                               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
		                               ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::SetNextWindowViewport(viewport->ID);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::Begin("Dockspace", nullptr, windowFlags);
		ImGui::PopStyleVar(3);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(370.0f, 64.0f));
		ImGuiID dockspaceId = ImGui::GetID("Dockspace");
		ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);
		ImGui::PopStyleVar();
	}

	ImGui::ShowDemoWindow();

	// End Dockspace
	ImGui::End();
}

static void RenderMain(double deltaTime) {
	UIManager::BeginFrame(deltaTime);
	RenderMainUI(deltaTime);
	UIManager::EndFrame();

	Renderer::Render(deltaTime);
}

static void RunFrame(double deltaTime) {
	ZoneScopedN("Engine::RunFrame");

	if (!State.Window->IsMinimized()) { RenderMain(deltaTime); }

	State.FrameCount++;
}

static void Update() {
	ZoneScopedN("Engine::Update");

	const double now       = WindowManager::GetTime();
	const double deltaTime = now - State.LastFrame;
	State.LastFrame        = now;

	Filesystem::Update();
	WindowManager::Update();
	if (State.Window->IsCloseRequested()) { State.Running = false; }

	RunFrame(deltaTime);
}

bool Engine::Initialize(const EngineOptions& options) {
	ZoneScopedN("Engine::Initialize");

	if (!Log::Initialize()) { return -1; }
	if (!Threading::Initialize()) { return -1; }
	if (!Filesystem::Initialize()) { return -1; }
	Filesystem::RegisterProtocol("assets", std::unique_ptr<FilesystemBackend>(new OSFilesystem("Assets")));
	Filesystem::RegisterProtocol("res", std::unique_ptr<FilesystemBackend>(new OSFilesystem("Resources")));
	if (!WindowManager::Initialize()) { return -1; }
	if (!Renderer::Initialize()) { return -1; }
	if (!ShaderManager::Initialize()) { return -1; }
	if (!UIManager::Initialize()) { return -1; }

	State.Window = std::make_unique<Window>("Luna", 1600, 900, false);
	if (!State.Window) { return -1; }
	State.Window->Maximize();
	Renderer::SetMainWindow(*State.Window);

	Renderer::SetScene(State.ActiveScene);

	RunFrame(0.0);

	State.Window->Show();

	State.Initialized = true;
	return true;
}

int Engine::Run() {
	if (!State.Initialized) { return -1; }

	State.FrameCount = 0;
	State.LastFrame  = WindowManager::GetTime();
	State.Running    = true;
	while (State.Running) {
		Update();
		FrameMark;
	}

	return 0;
}

void Engine::Shutdown() {
	ZoneScopedN("Engine::Shutdown");

	if (!State.Initialized) { return; }

	State.Window.reset();

	UIManager::Shutdown();
	ShaderManager::Shutdown();
	Renderer::Shutdown();
	WindowManager::Shutdown();
	Threading::Shutdown();
	Log::Shutdown();

	State.Initialized = false;
}

Window* Engine::GetMainWindow() {
	return State.Window.get();
}
}  // namespace Luna
