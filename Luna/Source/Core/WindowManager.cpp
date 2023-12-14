#include <GLFW/glfw3.h>

#include <Luna/Core/WindowManager.hpp>

namespace Luna {
struct CursorShape {
	const char* const Pixels;
	const int Width;
	const int Height;
	const int X;
	const int Y;
};

// clang-format off
static constexpr const CursorShape ResizeNWSE = {
	.Pixels = "XXXXXXX          "
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
	          "          XXXXXXX",
	.Width = 17,
	.Height = 17,
	.X = 8,
	.Y = 8
};
static constexpr const CursorShape ResizeNESW = {
	.Pixels = "          XXXXXXX"
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
	          "XXXXXXX          ",
	.Width = 17,
	.Height = 17,
	.X = 8,
	.Y = 8
};
static constexpr const CursorShape ResizeAll = {
	.Pixels = "           X           "
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
	          "           X           ",
	.Width = 23,
	.Height = 23,
	.X = 11,
	.Y = 11
};
// clang-format on

static struct WindowManagerState {
	std::array<GLFWcursor*, 9> Cursors;
} State;

static void GlfwErrorCallback(int errorCode, const char* errorDescription) {
	Log::Error("WindowManager", "GLFW Error {}: {}", errorCode, errorDescription);
}

bool WindowManager::Initialize() {
	glfwSetErrorCallback(GlfwErrorCallback);

	if (!glfwInit()) { return false; }
	if (!glfwVulkanSupported()) { return false; }

	State.Cursors[int(MouseCursor::Arrow)]     = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
	State.Cursors[int(MouseCursor::IBeam)]     = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
	State.Cursors[int(MouseCursor::Crosshair)] = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
	State.Cursors[int(MouseCursor::Hand)]      = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
	State.Cursors[int(MouseCursor::ResizeEW)]  = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
	State.Cursors[int(MouseCursor::ResizeNS)]  = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);

	const auto AddCursor = [](const CursorShape& shape) -> GLFWcursor* {
		const int pixelCount = shape.Width * shape.Height;
		std::vector<unsigned char> cursorPixels(pixelCount * 4);
		for (int i = 0; i < pixelCount; ++i) {
			if (shape.Pixels[i] == 'X') {
				cursorPixels[i * 4 + 0] = 0;
				cursorPixels[i * 4 + 1] = 0;
				cursorPixels[i * 4 + 2] = 0;
				cursorPixels[i * 4 + 3] = 255;
			} else if (shape.Pixels[i] == '.') {
				cursorPixels[i * 4 + 0] = 255;
				cursorPixels[i * 4 + 1] = 255;
				cursorPixels[i * 4 + 2] = 255;
				cursorPixels[i * 4 + 3] = 255;
			} else {
				cursorPixels[i * 4 + 0] = 0;
				cursorPixels[i * 4 + 1] = 0;
				cursorPixels[i * 4 + 2] = 0;
				cursorPixels[i * 4 + 3] = 0;
			}
		}
		const GLFWimage cursorImage{.width = shape.Width, .height = shape.Height, .pixels = cursorPixels.data()};

		return glfwCreateCursor(&cursorImage, shape.X, shape.Y);
	};

	State.Cursors[int(MouseCursor::ResizeNESW)] = AddCursor(ResizeNESW);
	State.Cursors[int(MouseCursor::ResizeNWSE)] = AddCursor(ResizeNWSE);
	State.Cursors[int(MouseCursor::ResizeAll)]  = AddCursor(ResizeAll);

	return true;
}

void WindowManager::Update() {
	glfwPollEvents();
}

void WindowManager::Shutdown() {
	for (auto cursor : State.Cursors) { glfwDestroyCursor(cursor); }

	glfwTerminate();
}

GLFWcursor* WindowManager::GetCursor(MouseCursor cursor) {
	return State.Cursors[int(cursor)];
}

std::vector<const char*> WindowManager::GetRequiredInstanceExtensions() {
	uint32_t extensionCount     = 0;
	const char** extensionNames = glfwGetRequiredInstanceExtensions(&extensionCount);

	return std::vector<const char*>(extensionNames, extensionNames + extensionCount);
}

double WindowManager::GetTime() {
	return glfwGetTime();
}
}  // namespace Luna
