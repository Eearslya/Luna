#include "Application.hpp"

#include "GlfwPlatform.hpp"
#include "Utility/Log.hpp"
#include "Vulkan/WSI.hpp"

namespace Luna {
int Application::Main(int argc, const char** argv) {
	Log::Initialize();

	auto app      = CreateApplication(argc, argv);
	auto platform = std::make_unique<GlfwPlatform>();
	app->Initialize(std::make_shared<Vulkan::WSI>(std::move(platform)));
	app->Start();
	app->Run();
	app->Stop();

	Log::Shutdown();

	return 0;
}

void Application::Run() {
	while (_wsi->IsAlive()) { Update(); }
}

void Application::Initialize(std::shared_ptr<Vulkan::WSI> wsi) {
	_wsi = wsi;
}
}  // namespace Luna
