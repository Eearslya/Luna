#pragma once

#include <Luna/Vulkan/WSI.hpp>

struct GLFWwindow;

namespace Luna {
class GlfwPlatform : public Vulkan::WSIPlatform {
 public:
	GlfwPlatform();
	GlfwPlatform(const GlfwPlatform&)   = delete;
	GlfwPlatform(GlfwPlatform&&)        = delete;
	void operator=(const GlfwPlatform&) = delete;
	void operator=(GlfwPlatform&&)      = delete;
	~GlfwPlatform() noexcept;

	virtual glm::uvec2 GetFramebufferSize() const override;
	virtual std::vector<const char*> GetRequiredDeviceExtensions() const override;
	virtual std::vector<const char*> GetRequiredInstanceExtensions() const override;
	virtual bool IsAlive() const override;

	virtual VkSurfaceKHR CreateSurface(VkInstance instance) override;
	virtual void Initialize() override;
	virtual void Update() override;
	virtual void Shutdown() override;

 private:
	GLFWwindow* _window = nullptr;
};
}  // namespace Luna
