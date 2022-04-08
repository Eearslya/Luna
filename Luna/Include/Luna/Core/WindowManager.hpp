#pragma once

struct GLFWwindow;

namespace Luna {
class WindowManager final {
	friend class Engine;

 public:
	~WindowManager() noexcept;

	void Update();

	static WindowManager* Get();

 private:
	WindowManager();

	static WindowManager* _instance;

	GLFWwindow* _window = nullptr;
};
}  // namespace Luna
