#include "Application.hpp"

#include "GlfwPlatform.hpp"
#include "Utility/Log.hpp"
#include "Vulkan/WSI.hpp"

namespace Luna {
int Application::Main(int argc, const char** argv) {
	Log::Initialize();

	auto app      = CreateApplication(argc, argv);
	auto platform = std::make_unique<GlfwPlatform>();
	try {
		app->Initialize(std::make_shared<Vulkan::WSI>(std::move(platform)));
		app->Start();
	} catch (const std::exception& e) {
		Log::Fatal("Luna", "Fatal exception caught when initializing application:\n\t{}", e.what());
		return 1;
	}
	try {
		app->Run();
	} catch (const std::exception& e) {
		Log::Fatal("Luna", "Fatal exception caught when running application:\n\t{}", e.what());
		return 1;
	}
	app->Stop();

	Log::Shutdown();

	return 0;
}

void Application::Run() {
	double lastTime = glfwGetTime();
	while (_wsi->IsAlive()) {
		const double now = glfwGetTime();
		const double dt  = now - lastTime;
		lastTime         = now;
		Update(dt);
	}
}

void Application::Initialize(std::shared_ptr<Vulkan::WSI> wsi) {
	_wsi = wsi;
}
}  // namespace Luna
