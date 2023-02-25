#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace Luna {
class Threading;

namespace Vulkan {
class Device;
struct SwapchainConfiguration;
class WSI;
class WSIPlatform;
}  // namespace Vulkan

class Application {
 public:
	Application();
	virtual ~Application() noexcept;

	virtual glm::uvec2 GetDefaultSize() const = 0;
	virtual std::string GetName() const       = 0;

	virtual void Start() {}
	virtual void Render() = 0;
	virtual void Stop() {}

	bool InitializeWSI(Vulkan::WSIPlatform* platform);
	int Run();

 protected:
	Vulkan::Device& GetDevice();

	virtual void OnSwapchainChanged(const Vulkan::SwapchainConfiguration& config) {}

 private:
	std::unique_ptr<Threading> _threading;
	std::unique_ptr<Vulkan::WSI> _wsi;
};

extern Application* CreateApplication(int argc, const char** argv);
}  // namespace Luna
