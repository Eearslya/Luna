#pragma once

#include <vector>

struct GLFWwindow;
typedef struct VkInstance_T* VkInstance;
typedef struct VkSurfaceKHR_T* VkSurfaceKHR;

namespace Luna {
class WindowManager final {
	friend class Engine;

 public:
	~WindowManager() noexcept;

	VkSurfaceKHR CreateSurface(VkInstance instance);
	std::vector<const char*> GetVulkanExtensions();
	void Update();

	static WindowManager* Get();

 private:
	WindowManager();

	static WindowManager* _instance;

	GLFWwindow* _window = nullptr;
};
}  // namespace Luna
