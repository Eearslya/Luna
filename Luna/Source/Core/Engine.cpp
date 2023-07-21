#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Log.hpp>
#include <Luna/Core/Threading.hpp>
#include <Luna/Core/Window.hpp>
#include <Luna/Core/WindowManager.hpp>

namespace Luna {
static struct EngineState {
	bool Initialized = false;
	bool Running     = false;
	std::unique_ptr<Window> Window;
} State;

static void Update() {
	WindowManager::Update();
	if (State.Window->IsCloseRequested()) { State.Running = false; }
}

bool Engine::Initialize() {
	if (!Log::Initialize()) { return false; }
	Log::SetLevel(Log::Level::Trace);
	Log::Info("Luna", "Luna Engine initializing...");

	if (!Threading::Initialize()) { return false; }
	if (!WindowManager::Initialize()) { return false; }

	State.Window = std::make_unique<Window>("Luna", 1600, 900, false);
	if (!State.Window) { return false; }
	State.Window->Maximize();

	State.Window->Show();

	State.Initialized = true;

	return true;
}

int Engine::Run() {
	if (!State.Initialized) { return -1; }

	State.Running = true;
	while (State.Running) { Update(); }

	return 0;
}

void Engine::Shutdown() {
	State.Window.reset();
	WindowManager::Shutdown();
	Threading::Shutdown();
	Log::Shutdown();
}

double Engine::GetTime() {
	return WindowManager::GetTime();
}
}  // namespace Luna
