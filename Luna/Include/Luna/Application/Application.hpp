#pragma once

#include <Luna/Utility/Delegate.hpp>
#include <glm/glm.hpp>
#include <memory>

namespace Luna {
class Threading;

namespace Vulkan {
class Device;
class ImGuiRenderer;
struct SwapchainConfiguration;
class WSI;
class WSIPlatform;
}  // namespace Vulkan

class Application {
 public:
	Application();
	virtual ~Application() noexcept;

	Vulkan::WSI& GetWSI() {
		return *_wsi;
	}

	virtual void OnStart() {}
	virtual void OnUpdate() = 0;
	virtual void OnImGuiRender() {}
	virtual void OnStop() {}

	bool InitializeWSI(Vulkan::WSIPlatform* platform);

	int Run();

	static Application* Get() {
		return _instance;
	}

 protected:
	Vulkan::Device& GetDevice();
	glm::uvec2 GetFramebufferSize() const;
	Vulkan::ImGuiRenderer& GetImGui();
	const Vulkan::SwapchainConfiguration& GetSwapchainConfig() const;
	void UpdateImGuiFontAtlas();

	Delegate<void(const Luna::Vulkan::SwapchainConfiguration&)> OnSwapchainChanged;

 private:
	static Application* _instance;

	std::unique_ptr<Threading> _threading;
	std::unique_ptr<Vulkan::WSI> _wsi;
	std::unique_ptr<Vulkan::ImGuiRenderer> _imguiRenderer;
};

extern Application* CreateApplication(int argc, const char** argv);
}  // namespace Luna
