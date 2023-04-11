#include <Luna/Application/Application.hpp>
#include <Luna/Platform/Filesystem.hpp>
#include <Luna/Platform/Windows/OSFilesystem.hpp>
#include <Luna/Project/ProjectFilesystem.hpp>
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
	Log::SetLevel(Log::Level::Debug);

	Log::Info("Luna", "Luna Engine initializing...");

	_filesystem = std::make_unique<Filesystem>();
	_threading  = std::make_unique<Threading>();

	_filesystem->RegisterProtocol("assets", std::unique_ptr<FilesystemBackend>(new OSFilesystem("Assets")));
	_filesystem->RegisterProtocol("cache", std::unique_ptr<FilesystemBackend>(new OSFilesystem("Cache")));
	_filesystem->RegisterProtocol("project", std::unique_ptr<FilesystemBackend>(new ProjectFilesystem()));
	_filesystem->RegisterProtocol("res", std::unique_ptr<FilesystemBackend>(new OSFilesystem("Resources")));
}

Application::~Application() noexcept {
	Log::Info("Luna", "Luna Engine shutting down...");
	// _imguiRenderer.reset();
	_wsi.reset();
	_filesystem.reset();
	_threading.reset();
	Log::Shutdown();

	_instance = nullptr;
}

bool Application::InitializeWSI(Vulkan::WSIPlatform* platform) {
	_wsi = std::make_unique<Vulkan::WSI>(platform);
	Log::Info("Luna", "WSI initialized.");

	_wsi->OnSwapchainChanged += [&](const Luna::Vulkan::SwapchainConfiguration& config) { OnSwapchainChanged(config); };

	// _imguiRenderer = std::make_unique<Vulkan::ImGuiRenderer>(*_wsi);

	return true;
}

int Application::Run() {
	Log::Info("Luna", "Starting application.");
	OnStart();
	while (_wsi->IsAlive()) {
		_filesystem->Update();
		_wsi->Update();

		_wsi->BeginFrame();
		const double smoothElapsedTime = _wsi->GetSmoothElapsedTime();
		const double smoothFrameTime   = _wsi->GetSmoothFrameTime();
		try {
			ZoneScopedN("Application::OnUpdate");
			OnUpdate(smoothFrameTime, smoothElapsedTime);
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

const Vulkan::SwapchainConfiguration& Application::GetSwapchainConfig() const {
	return _wsi->GetSwapchainConfig();
}
}  // namespace Luna
