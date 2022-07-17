#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <iostream>

#include "GlfwPlatform.hpp"
#include "ImGuiRenderer.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneHierarchyPanel.hpp"
#include "SceneRenderer.hpp"
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
		auto scene         = std::make_shared<Scene>();
		auto imguiRenderer = std::make_unique<ImGuiRenderer>(wsi);
		auto sceneRenderer = std::make_unique<SceneRenderer>(wsi);
		auto scenePanel    = std::make_unique<SceneHierarchyPanel>(scene);

		scene->CreateEntity("Camera");
		scene->CreateEntity("Light");
		scene->CreateEntity("Mesh");

		while (wsi.IsAlive()) {
			wsi.BeginFrame();
			imguiRenderer->BeginFrame();

			const auto frameIndex = wsi.GetAcquiredIndex();

			auto cmd = device.RequestCommandBuffer();

			ImVec2 viewportSize(0.0f, 0.0f);
			imguiRenderer->BeginDockspace();
			if (ImGui::BeginMainMenuBar()) {
				if (ImGui::BeginMenu("File")) {
					if (ImGui::MenuItem(ICON_FA_POWER_OFF " Exit")) { wsi.RequestShutdown(); }
					ImGui::EndMenu();
				}
				ImGui::EndMainMenuBar();
			}

			ImGui::ShowDemoWindow();

			scenePanel->Render();

			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSizeConstraints(
				ImVec2(256.0f, 256.0f), ImVec2(std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()));
			if (ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoCollapse)) {
				const ImVec2 windowMin = ImGui::GetWindowContentRegionMin();
				const ImVec2 windowMax = ImGui::GetWindowContentRegionMax();
				viewportSize           = ImVec2(windowMax.x - windowMin.x, windowMax.y - windowMin.y);

				sceneRenderer->SetImageSize(glm::uvec2(viewportSize.x, viewportSize.y));
				sceneRenderer->Render(cmd, *scene, frameIndex);
				auto& sceneImage = sceneRenderer->GetImage(frameIndex);
				if (sceneImage) { ImGui::Image(reinterpret_cast<ImTextureID>(&sceneImage->GetView()), viewportSize); }
			}
			ImGui::End();
			ImGui::PopStyleVar();
			imguiRenderer->EndDockspace();
			imguiRenderer->Render(cmd, frameIndex, true);

			device.Submit(cmd);

			wsi.EndFrame();
		}
	} catch (const std::exception& e) {
		std::cerr << "Fatal uncaught exception:\n\t" << e.what() << std::endl;
		return 1;
	}

	Log::Shutdown();

	return 0;
}
