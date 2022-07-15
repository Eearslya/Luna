#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <iostream>

#include "Utility/Log.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/CommandBuffer.hpp"
#include "Vulkan/Context.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/Shader.hpp"
#include "Vulkan/WSI.hpp"

using namespace Luna;

static constexpr const char* VertexShader = R"GLSL(
#version 460 core

void main() {}
)GLSL";

static constexpr const char* FragmentShader = R"GLSL(
#version 460 core

layout(location = 0) out vec4 outColor;

void main() {
  outColor = vec4(1, 1, 1, 1);
}
)GLSL";

static constexpr const char* ComputeShader = R"GLSL(
#version 460 core

layout(local_size_x = 64) in;

layout(std430, set = 0, binding = 0) readonly buffer BufferA {
  float InputA[];
};
layout(std430, set = 0, binding = 1) readonly buffer BufferB {
  float InputB[];
};
layout(std430, set = 1, binding = 0) writeonly buffer BufferC {
  float Output[];
};

void main() {
  uint ident = gl_GlobalInvocationID.x;
  Output[ident] = InputA[ident] * InputB[ident];
}
)GLSL";

class GlfwPlatform : public Vulkan::WSIPlatform {
 public:
	GlfwPlatform() {
		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

		_window = glfwCreateWindow(1600, 900, "Luna", nullptr, nullptr);
		glfwGetWindowSize(_window, &_windowSize.x, &_windowSize.y);
		glfwGetFramebufferSize(_window, &_framebufferSize.x, &_framebufferSize.y);
		GLFWmonitor* monitor         = glfwGetPrimaryMonitor();
		const GLFWvidmode* videoMode = glfwGetVideoMode(monitor);
		glfwSetWindowPos(_window, (videoMode->width - _windowSize.x) / 2, (videoMode->height - _windowSize.y) / 2);

		glfwShowWindow(_window);
	}

	virtual vk::SurfaceKHR CreateSurface(vk::Instance instance, vk::PhysicalDevice gpu) override {
		VkSurfaceKHR surface = VK_NULL_HANDLE;
		glfwCreateWindowSurface(instance, _window, nullptr, &surface);

		return surface;
	}

	virtual void DestroySurface(vk::Instance instance, vk::SurfaceKHR surface) override {
		instance.destroySurfaceKHR(surface);
	}

	virtual std::vector<const char*> GetInstanceExtensions() override {
		uint32_t extensionCount = 0;
		const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);

		return {extensions, extensions + extensionCount};
	}

	virtual std::vector<const char*> GetDeviceExtensions() override {
		return {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
	}

	virtual uint32_t GetSurfaceHeight() override {
		return _framebufferSize.y;
	}

	virtual uint32_t GetSurfaceWidth() override {
		return _framebufferSize.x;
	}

	virtual bool IsAlive() override {
		return !glfwWindowShouldClose(_window);
	}

	virtual void Update() override {
		glfwPollEvents();
	}

 private:
	GLFWwindow* _window = nullptr;

	glm::ivec2 _framebufferSize = glm::ivec2(0, 0);
	glm::ivec2 _windowSize      = glm::ivec2(1600, 900);
};

int main(int argc, const char** argv) {
	Log::Initialize();
	Log::SetLevel(Log::Level::Trace);

	try {
		auto platform = std::make_unique<GlfwPlatform>();
		Vulkan::WSI wsi(std::move(platform));
		auto& device = wsi.GetDevice();

		while (wsi.IsAlive()) {
			wsi.BeginFrame();

			auto cmd = device.RequestCommandBuffer();

			device.Submit(cmd);

			wsi.EndFrame();
		}
	} catch (const std::exception& e) {
		std::cerr << "Fatal uncaught exception when initializing Vulkan:\n\t" << e.what() << std::endl;
		return 1;
	}

	Log::Shutdown();

	return 0;
}
