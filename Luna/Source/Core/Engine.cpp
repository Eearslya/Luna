#include <imgui.h>

#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Input.hpp>
#include <Luna/Core/Window.hpp>
#include <Luna/Core/WindowManager.hpp>
#include <Luna/Editor/Editor.hpp>
#include <Luna/Platform/Filesystem.hpp>
#include <Luna/Platform/Windows/OSFilesystem.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Renderer/ShaderManager.hpp>
#include <Luna/Scene/Scene.hpp>
#include <Luna/UI/UIManager.hpp>
#include <Luna/Utility/BitOps.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Utility/Threading.hpp>
#include <Tracy/Tracy.hpp>

namespace Luna {
static struct EngineState {
	bool Initialized       = false;
	bool Running           = false;
	bool ShutdownRequested = false;
	std::unique_ptr<Window> Window;

	Scene ActiveScene;

	uint64_t FrameCount = 0;
	double LastFrame    = 0.0;
} State;

static void RenderMain(double deltaTime) {
	Renderer::Render(deltaTime);
}

static void Update() {
	ZoneScopedN("Engine::Update");

	const double now       = WindowManager::GetTime();
	const double deltaTime = now - State.LastFrame;
	State.LastFrame        = now;

	const double updateStart = WindowManager::GetTime();
	{
		ZoneScopedN("Systems");

		UIManager::BeginFrame(deltaTime);

		Filesystem::Update();
		ShaderManager::Update();
		WindowManager::Update();
		if (State.Window->IsCloseRequested()) { State.Running = false; }
		Editor::Update(deltaTime);
	}
	const double updateTime = WindowManager::GetTime() - updateStart;

	const double renderStart = WindowManager::GetTime();
	if (!State.Window->IsMinimized()) {
		ZoneScopedN("Render");

		Renderer::Render(deltaTime);
	}
	const double renderTime = WindowManager::GetTime() - renderStart;

	if (State.ShutdownRequested) { State.Running = false; }

	State.FrameCount++;
}

bool Engine::Initialize(const EngineOptions& options) {
	ZoneScopedN("Engine::Initialize");

	if (!Log::Initialize()) { return -1; }
	if (!Threading::Initialize()) { return -1; }
	if (!Filesystem::Initialize()) { return -1; }
	Filesystem::RegisterProtocol("res", std::unique_ptr<FilesystemBackend>(new OSFilesystem("Resources")));
	if (!WindowManager::Initialize()) { return -1; }
	if (!Renderer::Initialize()) { return -1; }
	if (!ShaderManager::Initialize()) { return -1; }
	if (!UIManager::Initialize()) { return -1; }

	State.Window = std::make_unique<Window>("Luna", 1600, 900, false);
	if (!State.Window) { return -1; }
	State.Window->Maximize();

	if (!Editor::Initialize()) { return -1; }

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

	Editor::Shutdown();

	State.Window.reset();

	UIManager::Shutdown();
	ShaderManager::Shutdown();
	Renderer::Shutdown();
	WindowManager::Shutdown();
	Threading::Shutdown();
	Log::Shutdown();

	State.Initialized = false;
}

Scene& Engine::GetActiveScene() {
	return State.ActiveScene;
}

Window* Engine::GetMainWindow() {
	return State.Window.get();
}

void Engine::RequestShutdown() {
	State.ShutdownRequested = true;
}
}  // namespace Luna
