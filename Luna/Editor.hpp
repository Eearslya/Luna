#pragma once

#include <filesystem>
#include <memory>

#include "Vulkan/Common.hpp"

struct ContentBrowserItem;
class ContentBrowserPanel;
class ImGuiRenderer;
class SceneRenderer;
namespace Luna {
class Scene;
class SceneHierarchyPanel;
}  // namespace Luna

class Editor {
 public:
	struct EditorResources {
		Luna::Vulkan::ImageHandle DirectoryIcon;
		Luna::Vulkan::ImageHandle FileIcon;
	};

	Editor();
	~Editor() noexcept;

	static Editor* Get() {
		return _instance;
	}

	void Run();

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

	std::unique_ptr<Luna::Vulkan::WSI> _wsi;
	EditorResources _resources;
	std::shared_ptr<Luna::Scene> _scene;
	std::unique_ptr<ImGuiRenderer> _imguiRenderer;
	std::unique_ptr<SceneRenderer> _sceneRenderer;
	std::unique_ptr<ContentBrowserPanel> _contentBrowserPanel;
	std::unique_ptr<Luna::SceneHierarchyPanel> _scenePanel;

	bool _showContentBrowser = true;
	bool _showDemoWindow     = false;
};
