#pragma once

#include <Luna/Utility/Delegate.hpp>
#include <glm/glm.hpp>
#include <memory>

namespace Luna {
class Filesystem;
class Threading;

namespace Vulkan {
class Device;
class ImGuiRenderer;
struct SwapchainConfiguration;
class WSI;
class WSIPlatform;
}  // namespace Vulkan

/**
 * The core of the Luna engine. Only one Application exists per program. This class is responsible for setting up
 * platform-specific utilities, windowing, graphics, and input.
 * This class is overridden by the program and provides a handful of methods that will be called on startup, shutdown,
 * update, and a few other callbacks.
 * The base class handles most of the low-level platform details, while the overridden class is responsible for loading
 * assets and submitting render tasks.
 */
class Application {
 public:
	Application();
	virtual ~Application() noexcept;

	Vulkan::WSI& GetWSI() {
		return *_wsi;
	}

	/**
	 * Called once on Application startup. At this point, it is expected all platform utilities such as filesystem,
	 * threading, and graphics are available to use.
	 */
	virtual void OnStart() {}

	/**
	 * Called once per update/frame. At this point the before-frame processes have been completed (e.g. swapchain acquire)
	 * and the application is ready to render. All client applications must override this function.
	 */
	virtual void OnUpdate() = 0;

	/**
	 * Called once per update/frame. This is where the application should build any ImGui UI they need to render.
	 */
	virtual void OnImGuiRender() {}

	/**
	 * Called once on Application shutdown. Platform utilities are still available at this point, so the application
	 * should take this time to clean up resources, save assets, etc.
	 */
	virtual void OnStop() {}

	/**
	 * Attaches the Window System Integration to the application. This is called once during main() and should not be used
	 * by the client application.
	 */
	bool InitializeWSI(Vulkan::WSIPlatform* platform);

	/**
	 * Starts the base application. This is called once during main() and should not be used by the client application.
	 */
	int Run();

	/**
	 * Retrieves the currently running Application.
	 */
	static Application* Get() {
		return _instance;
	}

 protected:
	/**
	 * Retrieves the application's Vulkan Device. This can be used to allocate graphical resources and record rendering
	 * commands.
	 */
	Vulkan::Device& GetDevice();

	/**
	 * Retrieves the current size of the framebuffer. This is the size, in pixels, of the renderable space of the
	 * application window (excluding title bar, borders, etc.)
	 */
	glm::uvec2 GetFramebufferSize() const;

	/**
	 * Retrieves the current ImGui Renderer. This can be used to set up fonts, styling, and to integrate with the Vulkan
	 * graphics backend.
	 */
	Vulkan::ImGuiRenderer& GetImGui();

	/**
	 * Retrieves the current Swapchain configuration. This includes information such as the current size and format of the
	 * swapchain.
	 */
	const Vulkan::SwapchainConfiguration& GetSwapchainConfig() const;
	void UpdateImGuiFontAtlas();

	/**
	 * Callback function which is called whenever the Vulkan Swapchain is changed (usually during a window resize).
	 */
	Delegate<void(const Luna::Vulkan::SwapchainConfiguration&)> OnSwapchainChanged;

 private:
	static Application* _instance;

	std::unique_ptr<Filesystem> _filesystem;
	std::unique_ptr<Threading> _threading;
	std::unique_ptr<Vulkan::WSI> _wsi;
	std::unique_ptr<Vulkan::ImGuiRenderer> _imguiRenderer;
};

/**
 * Main entrypoint for client applications. Every client application must implement this function and return a valid
 * child class of Application.
 */
extern Application* CreateApplication(int argc, const char** argv);
}  // namespace Luna
