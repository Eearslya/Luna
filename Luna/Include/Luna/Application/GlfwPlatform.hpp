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

	virtual InputAction GetButton(MouseButton button) const override;
	virtual glm::uvec2 GetFramebufferSize() const override;
	virtual InputAction GetKey(Key key) const override;
	virtual std::vector<const char*> GetRequiredDeviceExtensions() const override;
	virtual std::vector<const char*> GetRequiredInstanceExtensions() const override;
	virtual double GetTime() const override;
	virtual glm::uvec2 GetWindowSize() const override;
	virtual bool IsAlive() const override;

	virtual VkSurfaceKHR CreateSurface(VkInstance instance) override;
	virtual void Initialize() override;
	virtual void Update() override;
	virtual void Shutdown() override;

	virtual void SetCursorHidden(bool hidden) override;

 private:
	static void CallbackButton(GLFWwindow* window, int32_t button, int32_t action, int32_t mods);
	static void CallbackChar(GLFWwindow* window, uint32_t codepoint);
	static void CallbackKey(GLFWwindow* window, int32_t key, int32_t scancode, int32_t action, int32_t mods);
	static void CallbackPosition(GLFWwindow* window, double x, double y);
	static void CallbackScroll(GLFWwindow* window, double xOffset, double yOffset);

	GLFWwindow* _window = nullptr;
};
}  // namespace Luna
