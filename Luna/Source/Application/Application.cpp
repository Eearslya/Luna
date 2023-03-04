#include <Luna/Application/Application.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Vulkan/WSI.hpp>

namespace Luna {
Application::Application() {
	Log::Initialize();
	Log::SetLevel(Log::Level::Trace);

	Log::Info("Luna", "Luna Engine initializing...");
}

Application::~Application() noexcept {
	Log::Info("Luna", "Luna Engine shutting down...");
	_wsi.reset();
	Log::Shutdown();
}

bool Application::InitializeWSI(Vulkan::WSIPlatform* platform) {
	_wsi = std::make_unique<Vulkan::WSI>(platform);
	Log::Info("Luna", "WSI initialized.");

	return true;
}

int Application::Run() {
	Log::Info("Luna", "Starting application.");
	OnStart();
	while (_wsi->IsAlive()) {
		_wsi->Update();

		_wsi->BeginFrame();
		OnUpdate();
		_wsi->EndFrame();
	}
	OnStop();
	Log::Info("Luna", "Stopping application.");

	return 0;
}

Vulkan::Device& Application::GetDevice() {
	return _wsi->GetDevice();
}
}  // namespace Luna
