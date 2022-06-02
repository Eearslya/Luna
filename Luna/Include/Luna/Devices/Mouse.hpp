#pragma once

#include <Luna/Input/Common.hpp>
#include <Luna/Utility/Delegate.hpp>
#include <glm/glm.hpp>

struct GLFWcursor;
struct GLFWwindow;

namespace Luna {
class Window;

enum class MouseButton : uint8_t {
	Button1 = 0,
	Button2 = 1,
	Button3 = 2,
	Button4 = 3,
	Button5 = 4,
	Button6 = 5,
	Button7 = 6,
	Button8 = 7,
	Left    = Button1,
	Right   = Button2,
	Middle  = Button3
};

enum class CursorHotspot : uint8_t { UpperLeft, UpperRight, BottomLeft, BottomRight, Center };

enum class CursorStandard : uint32_t {
	Arrow     = 0x00036001,
	IBeam     = 0x00036002,
	Crosshair = 0x00036003,
	Hand      = 0x00036004,
	ResizeX   = 0x00036005,
	ResizeY   = 0x00036006
};

class Mouse {
 public:
	Mouse();

	static Mouse* Get() {
		return _instance;
	}

	void Update();

	const glm::dvec2& GetPosition() const {
		return _position;
	}
	const glm::dvec2& GetPositionDelta() const {
		return _positionDelta;
	}
	const glm::dvec2& GetScroll() const {
		return _scroll;
	}
	const glm::dvec2& GetScrollDelta() const {
		return _scrollDelta;
	}
	bool IsWindowSelected() const {
		return _windowSelected;
	}
	bool IsCursorHidden() const {
		return _cursorHidden;
	}

	InputAction GetButton(MouseButton button) const;

	void SetCursorHidden(bool hidden);
	void SetPosition(const glm::dvec2& position);

	CancellableDelegate<MouseButton, InputAction, InputMods>& OnButton() {
		return _onButton;
	}
	CancellableDelegate<bool>& OnEnter() {
		return _onEnter;
	}
	CancellableDelegate<glm::dvec2>& OnMoved() {
		return _onMoved;
	};
	CancellableDelegate<glm::dvec2>& OnScroll() {
		return _onScroll;
	}

 private:
	static Mouse* _instance;
	static void CallbackButton(GLFWwindow* window, int32_t button, int32_t action, int32_t mods);
	static void CallbackPosition(GLFWwindow* window, double x, double y);
	static void CallbackEnter(GLFWwindow* window, int32_t entered);
	static void CallbackScroll(GLFWwindow* window, double xOffset, double yOffset);

	glm::dvec2 _lastPosition;
	glm::dvec2 _lastScroll;
	glm::dvec2 _position;
	glm::dvec2 _positionDelta;
	glm::dvec2 _savedPosition;
	glm::dvec2 _scroll;
	glm::dvec2 _scrollDelta;
	bool _windowSelected = false;
	bool _cursorHidden   = false;

	CancellableDelegate<MouseButton, InputAction, InputMods> _onButton;
	CancellableDelegate<bool> _onEnter;
	CancellableDelegate<glm::dvec2> _onMoved;
	CancellableDelegate<glm::dvec2> _onScroll;
};
}  // namespace Luna
