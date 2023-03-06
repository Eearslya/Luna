#pragma once

#include <glm/glm.hpp>
#include <memory>

namespace Luna {
namespace Vulkan {
class Device;
class WSI;
class WSIPlatform;
}  // namespace Vulkan

class Application {
 public:
	Application();
	virtual ~Application() noexcept;

	virtual void OnStart() {}
	virtual void OnUpdate() = 0;
	virtual void OnStop() {}

	bool InitializeWSI(Vulkan::WSIPlatform* platform);

	int Run();

 protected:
	Vulkan::Device& GetDevice();
	glm::uvec2 GetFramebufferSize() const;

 private:
	std::unique_ptr<Vulkan::WSI> _wsi;
};

extern Application* CreateApplication(int argc, const char** argv);
}  // namespace Luna
