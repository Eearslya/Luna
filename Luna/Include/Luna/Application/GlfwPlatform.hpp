#pragma once

#include <Luna/Vulkan/WSI.hpp>
#include <atomic>
#include <glm/glm.hpp>
#include <string>

struct GLFWwindow;

namespace Luna {
class GlfwPlatform : public Vulkan::WSIPlatform {
 public:
	GlfwPlatform(const std::string& name, const glm::uvec2& startSize);

	virtual std::vector<const char*> GetRequiredInstanceExtensions() const override;
	virtual std::vector<const char*> GetRequiredDeviceExtensions() const override;
	virtual bool IsAlive() const override;

	virtual void Update() override;

 private:
	GLFWwindow* _window    = nullptr;
	glm::uvec2 _windowSize = {0, 0};
	std::atomic_bool _shutdownRequested;
};
}  // namespace Luna
