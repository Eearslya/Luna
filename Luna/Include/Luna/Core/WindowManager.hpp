#pragma once

#include <Luna/Common.hpp>

struct GLFWcursor;

namespace Luna {
enum class MouseCursor : uint8_t {
	Arrow,
	IBeam,
	Crosshair,
	Hand,
	ResizeNS,
	ResizeEW,
	ResizeNESW,
	ResizeNWSE,
	ResizeAll
};

class WindowManager final {
 public:
	static bool Initialize();
	static void Update();
	static void Shutdown();

	static GLFWcursor* GetCursor(MouseCursor cursor);
	static std::vector<const char*> GetRequiredInstanceExtensions();
	static double GetTime();
};
};  // namespace Luna
