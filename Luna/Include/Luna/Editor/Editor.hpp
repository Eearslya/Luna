#pragma once

namespace Luna {
class Editor final {
 public:
	static bool Initialize();
	static void Update(double deltaTime);
	static void Shutdown();
};
}  // namespace Luna
