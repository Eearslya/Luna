#pragma once

#include <memory>

#include "Vulkan/Common.hpp"

class ContentBrowserPanel;
class ImGuiRenderer;
class SceneRenderer;
namespace Luna {
class Scene;
class SceneHierarchyPanel;
}  // namespace Luna

class Editor {
 public:
	Editor();
	~Editor() noexcept;

	void Run();

 private:
	std::unique_ptr<Luna::Vulkan::WSI> _wsi;
	std::shared_ptr<Luna::Scene> _scene;
	std::unique_ptr<ImGuiRenderer> _imguiRenderer;
	std::unique_ptr<SceneRenderer> _sceneRenderer;
	std::unique_ptr<ContentBrowserPanel> _contentBrowserPanel;
	std::unique_ptr<Luna::SceneHierarchyPanel> _scenePanel;
};
