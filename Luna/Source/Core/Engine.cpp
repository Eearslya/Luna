#include <imgui.h>

#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Filesystem.hpp>
#include <Luna/Core/Log.hpp>
#include <Luna/Core/OSFilesystem.hpp>
#include <Luna/Core/Threading.hpp>
#include <Luna/Core/Window.hpp>
#include <Luna/Core/WindowManager.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Renderer/ShaderManager.hpp>
#include <Luna/Renderer/UIManager.hpp>

namespace Luna {
static struct EngineState {
	bool Initialized = false;
	bool Running     = false;
	std::unique_ptr<Window> Window;

	uint64_t FrameCount = 0;
} State;

static void Update() {
	Filesystem::Update();
	ShaderManager::Update();
	WindowManager::Update();
	if (State.Window->IsCloseRequested()) { State.Running = false; }

	UIManager::BeginFrame();
	ImGui::ShowDemoWindow();
	if (!State.Window->IsMinimized()) { Renderer::Render(); }

	++State.FrameCount;
}

bool Engine::Initialize() {
	if (!Log::Initialize()) { return false; }
	Log::SetLevel(Log::Level::Trace);
	Log::Info("Luna", "Luna Engine initializing...");

	if (!Threading::Initialize()) { return false; }
	if (!Filesystem::Initialize()) { return false; }
	Filesystem::RegisterProtocol("res", std::unique_ptr<FilesystemBackend>(new OSFilesystem("Resources")));
	if (!WindowManager::Initialize()) { return false; }
	if (!Renderer::Initialize()) { return false; }
	if (!ShaderManager::Initialize()) { return false; }
	if (!UIManager::Initialize()) { return false; }

	State.Window = std::make_unique<Window>("Luna", 1600, 900, false);
	if (!State.Window) { return false; }
	State.Window->Maximize();

	State.Window->Show();

	State.Initialized = true;

	return true;
}

int Engine::Run() {
	if (!State.Initialized) { return -1; }

	State.FrameCount = 0;
	State.Running    = true;
	while (State.Running) { Update(); }

	return 0;
}

void Engine::Shutdown() {
	State.Window.reset();
	UIManager::Shutdown();
	ShaderManager::Shutdown();
	Renderer::Shutdown();
	WindowManager::Shutdown();
	Filesystem::Shutdown();
	Threading::Shutdown();
	Log::Shutdown();
}

double Engine::GetTime() {
	return WindowManager::GetTime();
}

Window* Engine::GetMainWindow() {
	return State.Window.get();
}
}  // namespace Luna
