#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <iostream>

#include "GlfwPlatform.hpp"
#include "ImGuiRenderer.hpp"
#include "Utility/Log.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/CommandBuffer.hpp"
#include "Vulkan/Context.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/RenderPass.hpp"
#include "Vulkan/Shader.hpp"
#include "Vulkan/WSI.hpp"

using namespace Luna;

int main(int argc, const char** argv) {
	Log::Initialize();
	Log::SetLevel(Log::Level::Trace);

	try {
		auto platform = std::make_unique<GlfwPlatform>();
		Vulkan::WSI wsi(std::move(platform));
		auto& device       = wsi.GetDevice();
		auto imguiRenderer = std::make_unique<ImGuiRenderer>(wsi);

		while (wsi.IsAlive()) {
			wsi.BeginFrame();
			imguiRenderer->BeginFrame();

			auto cmd = device.RequestCommandBuffer();

			auto rpInfo           = device.GetStockRenderPass();
			rpInfo.ClearColors[0] = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
			cmd->BeginRenderPass(rpInfo);
			cmd->EndRenderPass();

			ImGui::ShowDemoWindow();
			imguiRenderer->Render(cmd, device.GetFrameIndex());

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
