#pragma once

#include <Luna/Editor/EditorWindow.hpp>
#include <Luna/Utility/Path.hpp>

namespace Luna {
class MeshImportWindow final : public EditorWindow {
 public:
	MeshImportWindow(const Path& meshPath);

	virtual bool Closed() override;
	virtual void OnProjectChanged() override;
	virtual void Update(double deltaTime) override;

 private:
	const Path& _meshPath;
	bool _opened = false;
	bool _closed = false;
};
}  // namespace Luna
