#pragma once

#include <filesystem>

class ContentBrowserPanel {
 public:
	ContentBrowserPanel() = default;

	void Render(bool* show);

 private:
	const std::filesystem::path _assetsDirectory = "Assets";
};
