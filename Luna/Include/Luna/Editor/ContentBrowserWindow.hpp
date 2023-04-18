#pragma once

#include <Luna/Editor/EditorWindow.hpp>
#include <Luna/Utility/Path.hpp>
#include <vector>

namespace Luna {
class ContentBrowserWindow final : public EditorWindow {
 public:
	ContentBrowserWindow();

	virtual void OnProjectChanged() override;
	virtual void Update(double deltaTime) override;

 private:
	void ChangeDirectory(const Path& directory);

	Path _currentDirectory;
	std::vector<Path> _directories;
	std::vector<Path> _files;
	bool _viewSources = false;
};
}  // namespace Luna
