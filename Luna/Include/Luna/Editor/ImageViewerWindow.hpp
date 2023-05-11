#pragma once

#include <Luna/Editor/EditorWindow.hpp>

namespace Luna {
class ImageViewerWindow final : public EditorWindow {
 public:
	ImageViewerWindow();
	virtual void Update(double deltaTime) override;
};
}  // namespace Luna
