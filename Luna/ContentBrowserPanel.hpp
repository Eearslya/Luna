#pragma once

#include <filesystem>

class ContentBrowserPanel {
 public:
	ContentBrowserPanel() = default;

	void Render();

 private:
	const std::filesystem::path _assetsDirectory = "Assets";
};
