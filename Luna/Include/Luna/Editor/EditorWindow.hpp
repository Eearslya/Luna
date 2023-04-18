#pragma once

namespace Luna {
class EditorWindow {
 public:
	virtual ~EditorWindow() = default;

	virtual bool Closed() {
		return false;
	}
	virtual void OnProjectChanged() {}
	virtual void Update(double deltaTime) = 0;
};
}  // namespace Luna
