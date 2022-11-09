#include <Luna/Application/Application.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Vulkan/WSI.hpp>

namespace Luna {
Application::Application() {
	Log::Initialize();
#ifdef _DEBUG
	Log::SetLevel(Log::Level::Trace);
#endif
}

Application::~Application() noexcept {
	Log::Shutdown();
}

bool Application::InitializeWSI(Vulkan::WSIPlatform* platform) {
	_wsi = std::make_unique<Vulkan::WSI>(platform);

	return true;
}

int Application::Run() {
	while (_wsi->IsAlive()) {
		_wsi->Update();

		_wsi->BeginFrame();
		Render();
		_wsi->EndFrame();
	}

	return 0;
}
}  // namespace Luna
