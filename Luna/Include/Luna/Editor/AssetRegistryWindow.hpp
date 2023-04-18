#pragma once

#include <Luna/Editor/EditorWindow.hpp>

namespace Luna {
class AssetRegistryWindow final : public EditorWindow {
 public:
	virtual bool Closed() override;
	virtual void Update(double deltaTime) override;

 private:
	bool _closed = false;
};
}  // namespace Luna
