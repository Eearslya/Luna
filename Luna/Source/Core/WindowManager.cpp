#include <GLFW/glfw3.h>

#include <Luna/Core/WindowManager.hpp>
#include <Luna/Utility/Log.hpp>
#include <Tracy/Tracy.hpp>

namespace Luna {
// clang-format off
static const char* ResizeNWSEPixels = {
	"XXXXXXX          "
	"X.....X          "
	"X....X           "
	"X...X            "
	"X..X.X           "
	"X.X X.X          "
	"XX   X.X         "
	"      X.X        "
	"       X.X       "
	"        X.X      "
	"         X.X   XX"
	"          X.X X.X"
	"           X.X..X"
	"            X...X"
	"           X....X"
	"          X.....X"
	"          XXXXXXX"
};
static const int ResizeNWSEWidth    = 17;
static const int ResizeNWSEHeight   = 17;
static const int ResizeNWSEX        = 8;
static const int ResizeNWSEY        = 8;

static const char* ResizeNESWPixels = {
  "          XXXXXXX"
	"          X.....X"
	"           X....X"
	"            X...X"
	"           X.X..X"
	"          X.X X.X"
	"         X.X   XX"
	"        X.X      "
	"       X.X       "
	"      X.X        "
	"XX   X.X         "
	"X.X X.X          "
	"X..X.X           "
	"X...X            "
	"X....X           "
	"X.....X          "
	"XXXXXXX          "
};
static const int ResizeNESWWidth    = 17;
static const int ResizeNESWHeight   = 17;
static const int ResizeNESWX        = 8;
static const int ResizeNESWY        = 8;

static const char* ResizeAllPixels = {
  "           X           "
	"          X.X          "
	"         X...X         "
	"        X.....X        "
	"       X.......X       "
	"       XXXX.XXXX       "
	"          X.X          "
	"    XX    X.X    XX    "
	"   X.X    X.X    X.X   "
	"  X..X    X.X    X..X  "
	" X...XXXXXX.XXXXXX...X "
	"X.....................X"
	" X...XXXXXX.XXXXXX...X "
	"  X..X    X.X    X..X  "
	"   X.X    X.X    X.X   "
	"    XX    X.X    XX    "
	"          X.X          "
	"       XXXX.XXXX       "
	"       X.......X       "
	"        X.....X        "
	"         X...X         "
	"          X.X          "
	"           X           "
};
static const int ResizeAllWidth    = 23;
static const int ResizeAllHeight   = 23;
static const int ResizeAllX        = 11;
static const int ResizeAllY        = 11;
// clang-format on

static struct GlfwState { std::array<GLFWcursor*, 9> Cursors; } State;

static void GlfwErrorCallback(int errorCode, const char* errorDescription) {
	Log::Error("WindowManager", "GLFW Error {}: {}", errorCode, errorDescription);
}

bool WindowManager::Initialize() {
	ZoneScopedN("WindowManager::Initialize");

	glfwSetErrorCallback(GlfwErrorCallback);

	if (!glfwInit()) { return false; }

	State.Cursors[int(MouseCursor::Arrow)]     = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
	State.Cursors[int(MouseCursor::IBeam)]     = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
	State.Cursors[int(MouseCursor::Crosshair)] = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
	State.Cursors[int(MouseCursor::Hand)]      = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
	State.Cursors[int(MouseCursor::ResizeEW)]  = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
	State.Cursors[int(MouseCursor::ResizeNS)]  = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);

#define AddCursor(shape)                                                             \
	do {                                                                               \
		const char* pixels   = shape##Pixels;                                            \
		const int width      = shape##Width;                                             \
		const int height     = shape##Height;                                            \
		const int x          = shape##X;                                                 \
		const int y          = shape##Y;                                                 \
		const int pixelCount = width * height;                                           \
		unsigned char cursorPixels[pixelCount * 4];                                      \
		for (int i = 0; i < pixelCount; ++i) {                                           \
			if (pixels[i] == 'X') {                                                        \
				cursorPixels[i * 4 + 0] = 0;                                                 \
				cursorPixels[i * 4 + 1] = 0;                                                 \
				cursorPixels[i * 4 + 2] = 0;                                                 \
				cursorPixels[i * 4 + 3] = 255;                                               \
			} else if (pixels[i] == '.') {                                                 \
				cursorPixels[i * 4 + 0] = 255;                                               \
				cursorPixels[i * 4 + 1] = 255;                                               \
				cursorPixels[i * 4 + 2] = 255;                                               \
				cursorPixels[i * 4 + 3] = 255;                                               \
			} else {                                                                       \
				cursorPixels[i * 4 + 0] = 0;                                                 \
				cursorPixels[i * 4 + 1] = 0;                                                 \
				cursorPixels[i * 4 + 2] = 0;                                                 \
				cursorPixels[i * 4 + 3] = 0;                                                 \
			}                                                                              \
		}                                                                                \
		GLFWimage cursorImage{.width = width, .height = height, .pixels = cursorPixels}; \
		State.Cursors[int(MouseCursor::shape)] = glfwCreateCursor(&cursorImage, x, y);   \
	} while (0)
	AddCursor(ResizeNESW);
	AddCursor(ResizeNWSE);
	AddCursor(ResizeAll);

	return true;
}

void WindowManager::Update() {
	ZoneScopedN("WindowManager::Update");

	glfwPollEvents();
}

void WindowManager::Shutdown() {
	ZoneScopedN("WindowManager::Shutdown");

	for (auto cursor : State.Cursors) { glfwDestroyCursor(cursor); }

	glfwTerminate();
}

GLFWcursor* WindowManager::GetCursor(MouseCursor cursor) {
	return State.Cursors[int(cursor)];
}

std::vector<const char*> WindowManager::GetRequiredInstanceExtensions() {
	uint32_t extensionCount = 0;
	const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);

	return std::vector<const char*>(extensions, extensions + extensionCount);
}

double WindowManager::GetTime() {
	return glfwGetTime();
}
}  // namespace Luna
