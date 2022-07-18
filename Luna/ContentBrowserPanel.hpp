#pragma once

#include <filesystem>

enum class ContentBrowserItemType { Directory = 0, File = 1 };

struct ContentBrowserItem {
	ContentBrowserItemType Type;
	std::filesystem::path FilePath;
};

class ContentBrowserPanel {
 public:
	ContentBrowserPanel();

	void Render(bool* show);

 private:
	std::filesystem::path _currentDirectory;
	ContentBrowserItem _currentDragDropItem;
};
