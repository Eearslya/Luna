#include "Editor.hpp"

#include <imgui_internal.h>
#include <stb_image.h>

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

Editor* Editor::_instance = nullptr;

Editor::Editor() {
	_instance = this;

	Log::Initialize();
	Log::SetLevel(Log::Level::Trace);

	auto platform = std::make_unique<GlfwPlatform>();
	_wsi          = std::make_unique<Vulkan::WSI>(std::move(platform));

	LoadResources();

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

		_imguiRenderer->BeginDockspace();
		if (ImGui::BeginMainMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				if (ImGui::MenuItem(ICON_FA_DOWNLOAD " Save Scene")) { SaveScene(); }
				if (ImGui::MenuItem(ICON_FA_POWER_OFF " Exit")) { _wsi->RequestShutdown(); }
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Window")) {
				ImGui::MenuItem(ICON_FA_FOLDER_TREE " Content Browser", nullptr, &_showContentBrowser);
				ImGui::Separator();
				ImGui::MenuItem(ICON_FA_DESKTOP " ImGui Demo", nullptr, &_showDemoWindow);

				ImGui::EndMenu();
			}

			ImGui::EndMainMenuBar();
		}

		if (_showDemoWindow) { ImGui::ShowDemoWindow(&_showDemoWindow); }
		if (_showContentBrowser) { _contentBrowserPanel->Render(&_showContentBrowser); }
		_scenePanel->Render();
		RenderViewport(cmd);

		_imguiRenderer->EndDockspace();
		_imguiRenderer->Render(cmd, frameIndex, true);

		device.Submit(cmd);

		_wsi->EndFrame();
	}
}

void Editor::AcceptContent(const ContentBrowserItem& item) {
	if (!IsContentAccepted(item)) { return; }

	if (item.Type == ContentBrowserItemType::File) {
		const auto extension = item.FilePath.extension();
		if (extension.string() == ".scene") {
			SceneSerializer serializer(*_scene);
			serializer.Deserialize(AssetsDirectory / item.FilePath);
		}
	}
}

bool Editor::IsContentAccepted(const ContentBrowserItem& item) {
	if (item.Type == ContentBrowserItemType::Directory) { return false; }

	const auto extension = item.FilePath.extension().string();
	if (extension == ".scene") { return true; }

	return false;
}

void Editor::LoadResources() {
	const auto LoadTexture = [&](const std::filesystem::path& imageFile) -> Vulkan::ImageHandle {
		int width, height, components;
		const auto imageFileStr = imageFile.string();
		stbi_uc* pixels         = stbi_load(imageFileStr.c_str(), &width, &height, &components, STBI_rgb_alpha);
		if (!pixels) { return {}; }

		const Vulkan::ImageInitialData initialData{.Data = pixels};
		const Vulkan::ImageCreateInfo imageCI =
			Vulkan::ImageCreateInfo::Immutable2D(width, height, vk::Format::eR8G8B8A8Unorm, true);
		auto handle = _wsi->GetDevice().CreateImage(imageCI, &initialData);

		stbi_image_free(pixels);

		return handle;
	};

	_resources.DirectoryIcon = LoadTexture("Resources/Icons/Directory.png");
	_resources.FileIcon      = LoadTexture("Resources/Icons/File.png");
}

void Editor::RenderViewport(Vulkan::CommandBufferHandle& cmd) {
	ImVec2 viewportSize(0.0f, 0.0f);
	const auto frameIndex = _wsi->GetAcquiredIndex();

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
		if (sceneImage) {
			ImGui::Image(reinterpret_cast<ImTextureID>(&sceneImage->GetView()), viewportSize);

			if (ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* payload =
				      ImGui::AcceptDragDropPayload("ContentBrowserItem", ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
					const ContentBrowserItem* item = reinterpret_cast<const ContentBrowserItem*>(payload->Data);
					AcceptContent(*item);
				}

				// The default drag-drop rect is outside of the window's clip rect, so we have to draw our own inside of the
				// clip rect instead.
				const ImGuiPayload* payload = ImGui::GetDragDropPayload();
				if (payload && payload->Preview &&
				    IsContentAccepted(*reinterpret_cast<const ContentBrowserItem*>(payload->Data))) {
					ImGuiWindow* window = ImGui::GetCurrentWindow();
					window->DrawList->AddRect(window->ClipRect.Min + ImVec2(1.0f, 1.0f),
					                          window->ClipRect.Max - ImVec2(1.0f, 1.0f),
					                          ImGui::GetColorU32(ImGuiCol_DragDropTarget),
					                          0.0f,
					                          0,
					                          2.0f);
				}

				ImGui::EndDragDropTarget();
			}
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();
}

void Editor::SaveScene() {
	const auto scenePath = _scene->GetSceneAssetPath();
	if (!scenePath.empty()) {
		SceneSerializer serializer(*_scene);
		serializer.Serialize(scenePath);
	}
}
