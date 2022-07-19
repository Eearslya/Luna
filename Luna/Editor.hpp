#pragma once

#include <filesystem>
#include <memory>

#include "Application/Application.hpp"
#include "Vulkan/Common.hpp"

struct ContentBrowserItem;
class ContentBrowserPanel;
class MeshImportPanel;
class SceneHierarchyPanel;
class SceneRenderer;
namespace Luna {
class ImGuiRenderer;
class Scene;
}  // namespace Luna

class Editor : public Luna::Application {
 public:
	struct EditorResources {
		Luna::Vulkan::ImageHandle DirectoryIcon;
		Luna::Vulkan::ImageHandle FileIcon;
	};

	virtual void Start() override;
	virtual void Stop() override;
	virtual void Update() override;

	static Editor* Get() {
		return _instance;
	}

	EditorResources& GetResources() {
		return _resources;
	}
	void RequestContent(const ContentBrowserItem& item);

	static inline const std::filesystem::path AssetsDirectory = "Assets";

 private:
	static Editor* _instance;

	void AcceptContent(const ContentBrowserItem& item);
	bool IsContentAccepted(const ContentBrowserItem& item);
	void LoadResources();
	void RenderViewport(Luna::Vulkan::CommandBufferHandle& cmd);
	void SaveScene();
	void StyleImGui();

	EditorResources _resources;
	std::shared_ptr<Luna::Scene> _scene;
	std::unique_ptr<Luna::ImGuiRenderer> _imguiRenderer;
	std::unique_ptr<SceneRenderer> _sceneRenderer;
	std::unique_ptr<ContentBrowserPanel> _contentBrowserPanel;
	std::unique_ptr<SceneHierarchyPanel> _scenePanel;
	std::unique_ptr<MeshImportPanel> _meshImportPanel;

	bool _showContentBrowser = true;
	bool _showDemoWindow     = false;
};
