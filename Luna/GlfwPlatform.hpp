#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>

#include "Input.hpp"
#include "Vulkan/WSI.hpp"

class GlfwPlatform : public Luna::Vulkan::WSIPlatform {
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

		glfwSetKeyCallback(_window, CallbackKey);
		glfwSetCharCallback(_window, CallbackChar);
		glfwSetMouseButtonCallback(_window, CallbackButton);
		glfwSetCursorPosCallback(_window, CallbackPosition);
		glfwSetScrollCallback(_window, CallbackScroll);
		Input::AttachWindow(_window);

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

	virtual uint32_t GetWindowHeight() override {
		return _windowSize.y;
	}

	virtual uint32_t GetWindowWidth() override {
		return _windowSize.x;
	}

	virtual bool IsAlive() override {
		return !glfwWindowShouldClose(_window);
	}

	virtual void Update() override {
		glfwPollEvents();
	}

 private:
	static void CallbackKey(GLFWwindow* window, int32_t key, int32_t scancode, int32_t action, int32_t mods) {
		Input::OnKey(static_cast<Key>(key), static_cast<InputAction>(action), InputMods(mods));
	}

	static void CallbackChar(GLFWwindow* window, uint32_t codepoint) {
		Input::OnChar(static_cast<char>(codepoint));
	}

	static void CallbackButton(GLFWwindow* window, int32_t button, int32_t action, int32_t mods) {
		Input::OnButton(static_cast<MouseButton>(button), static_cast<InputAction>(action), InputMods(mods));
	}

	static void CallbackPosition(GLFWwindow* window, double x, double y) {
		Input::OnMoved({x, y});
	}

	static void CallbackScroll(GLFWwindow* window, double xOffset, double yOffset) {
		Input::OnScroll({xOffset, yOffset});
	}

	GLFWwindow* _window = nullptr;

	glm::ivec2 _framebufferSize = glm::ivec2(0, 0);
	glm::ivec2 _windowSize      = glm::ivec2(1600, 900);
};
