#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace Luna {
namespace Vulkan {
class WSI;
class WSIPlatform;
}  // namespace Vulkan

class Application {
 public:
	Application();
	virtual ~Application() noexcept;

	virtual glm::uvec2 GetDefaultSize() const = 0;
	virtual std::string GetName() const       = 0;

	virtual void Render() = 0;

	bool InitializeWSI(Vulkan::WSIPlatform* platform);
	int Run();

 protected:
 private:
	std::unique_ptr<Vulkan::WSI> _wsi;
};

extern Application* CreateApplication(int argc, const char** argv);
}  // namespace Luna
