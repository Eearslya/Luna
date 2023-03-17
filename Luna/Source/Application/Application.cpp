#include <Luna/Application/Application.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Utility/Threading.hpp>
#include <Luna/Vulkan/ImGuiRenderer.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/WSI.hpp>
#include <Tracy/Tracy.hpp>

namespace Luna {
Application* Application::_instance = nullptr;

Application::Application() {
	_instance = this;

	Log::Initialize();
	Log::SetLevel(Log::Level::Trace);

	Log::Info("Luna", "Luna Engine initializing...");

	_threading = std::make_unique<Threading>();
}

Application::~Application() noexcept {
	Log::Info("Luna", "Luna Engine shutting down...");
	_imguiRenderer.reset();
	_wsi.reset();
	_threading.reset();
	Log::Shutdown();

	_instance = nullptr;
}

bool Application::InitializeWSI(Vulkan::WSIPlatform* platform) {
	_wsi = std::make_unique<Vulkan::WSI>(platform);
	Log::Info("Luna", "WSI initialized.");

	_wsi->OnSwapchainChanged += [&](const Luna::Vulkan::SwapchainConfiguration& config) { OnSwapchainChanged(config); };

	_imguiRenderer = std::make_unique<Vulkan::ImGuiRenderer>(*_wsi);

	return true;
}

int Application::Run() {
	Log::Info("Luna", "Starting application.");
	OnStart();
	while (_wsi->IsAlive()) {
		_wsi->Update();

		_wsi->BeginFrame();
		try {
			ZoneScopedN("Application::OnUpdate");
			OnUpdate();
		} catch (const std::exception& e) {
			Log::Fatal("Luna", "Fatal exception occurred while updating application!");
			Log::Fatal("Luna", "{}", e.what());
			break;
		}
		{
			ZoneScopedN("Application::OnImGuiRender");
			// _imguiRenderer->BeginFrame();
			// OnImGuiRender();
			// _imguiRenderer->Render(false);
		}
		_wsi->EndFrame();

		FrameMark;
	}
	OnStop();
	Log::Info("Luna", "Stopping application.");

	return 0;
}

Vulkan::Device& Application::GetDevice() {
	return _wsi->GetDevice();
}

glm::uvec2 Application::GetFramebufferSize() const {
	return _wsi->GetFramebufferSize();
}

Vulkan::ImGuiRenderer& Application::GetImGui() {
	return *_imguiRenderer;
}

const Vulkan::SwapchainConfiguration& Application::GetSwapchainConfig() const {
	return _wsi->GetSwapchainConfig();
}

void Application::UpdateImGuiFontAtlas() {
	_imguiRenderer->UpdateFontAtlas();
}
}  // namespace Luna
