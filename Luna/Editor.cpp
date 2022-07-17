#include "Editor.hpp"

#include <glm/glm.hpp>

#include "ContentBrowserPanel.hpp"
#include "GlfwPlatform.hpp"
#include "ImGuiRenderer.hpp"
#include "Scene/CameraComponent.hpp"
#include "Scene/Entity.hpp"
#include "Scene/MeshComponent.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneHierarchyPanel.hpp"
#include "Scene/SceneSerializer.hpp"
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

Editor::Editor() {
	Log::Initialize();
	Log::SetLevel(Log::Level::Trace);

	auto platform        = std::make_unique<GlfwPlatform>();
	_wsi                 = std::make_unique<Vulkan::WSI>(std::move(platform));
	_scene               = std::make_shared<Scene>();
	_imguiRenderer       = std::make_unique<ImGuiRenderer>(*_wsi);
	_sceneRenderer       = std::make_unique<SceneRenderer>(*_wsi);
	_contentBrowserPanel = std::make_unique<ContentBrowserPanel>();
	_scenePanel          = std::make_unique<SceneHierarchyPanel>(_scene);
}

Editor::~Editor() noexcept {
	Log::Shutdown();
}

void Editor::Run() {
	auto& device = _wsi->GetDevice();

	while (_wsi->IsAlive()) {
		_wsi->BeginFrame();
		_imguiRenderer->BeginFrame();

		const auto frameIndex = _wsi->GetAcquiredIndex();

		auto cmd = device.RequestCommandBuffer();

		ImVec2 viewportSize(0.0f, 0.0f);
		_imguiRenderer->BeginDockspace();
		if (ImGui::BeginMainMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				if (ImGui::MenuItem(ICON_FA_DOWNLOAD " Serialize")) {
					SceneSerializer serializer(*_scene);
					serializer.Serialize("Examples/TestScene.scene");
				}
				if (ImGui::MenuItem(ICON_FA_UPLOAD " Deserialize")) {
					SceneSerializer serializer(*_scene);
					serializer.Deserialize("Examples/TestScene.scene");
				}
				if (ImGui::MenuItem(ICON_FA_POWER_OFF " Exit")) { _wsi->RequestShutdown(); }
				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}

		ImGui::ShowDemoWindow();

		_contentBrowserPanel->Render();
		_scenePanel->Render();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSizeConstraints(
			ImVec2(256.0f, 256.0f), ImVec2(std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()));
		if (ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoCollapse)) {
			const ImVec2 windowMin = ImGui::GetWindowContentRegionMin();
			const ImVec2 windowMax = ImGui::GetWindowContentRegionMax();
			viewportSize           = ImVec2(windowMax.x - windowMin.x, windowMax.y - windowMin.y);

			_sceneRenderer->SetImageSize(glm::uvec2(viewportSize.x, viewportSize.y));
			_sceneRenderer->Render(cmd, *_scene, frameIndex);
			auto& sceneImage = _sceneRenderer->GetImage(frameIndex);
			if (sceneImage) { ImGui::Image(reinterpret_cast<ImTextureID>(&sceneImage->GetView()), viewportSize); }
		}
		ImGui::End();
		ImGui::PopStyleVar();
		_imguiRenderer->EndDockspace();
		_imguiRenderer->Render(cmd, frameIndex, true);

		device.Submit(cmd);

		_wsi->EndFrame();
	}
}
