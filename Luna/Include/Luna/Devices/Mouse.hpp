#pragma once

#include <Luna/Core/Module.hpp>
#include <Luna/Devices/Window.hpp>
#include <Luna/Input/Common.hpp>
#include <Luna/Math/Vec2.hpp>

struct GLFWcursor;

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

class Mouse : public Module::Registrar<Mouse> {
	static inline const bool Registered = Register("Mouse", Stage::Pre, Depends<Window>());

 public:
	Mouse();
	~Mouse() noexcept;

	virtual void Update() override;

	const Vec2d& GetPosition() const {
		return _position;
	}
	const Vec2d& GetPositionDelta() const {
		return _positionDelta;
	}
	const Vec2d& GetScroll() const {
		return _scroll;
	}
	const Vec2d& GetScrollDelta() const {
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
	void SetPosition(const Vec2d& position);

	CancellableDelegate<MouseButton, InputAction, InputMods>& OnButton() {
		return _onButton;
	}
	CancellableDelegate<bool>& OnEnter() {
		return _onEnter;
	}
	CancellableDelegate<Vec2d>& OnMoved() {
		return _onMoved;
	};
	CancellableDelegate<Vec2d>& OnScroll() {
		return _onScroll;
	}

 private:
	static void CallbackButton(GLFWwindow* window, int32_t button, int32_t action, int32_t mods);
	static void CallbackPosition(GLFWwindow* window, double x, double y);
	static void CallbackEnter(GLFWwindow* window, int32_t entered);
	static void CallbackScroll(GLFWwindow* window, double xOffset, double yOffset);

	Vec2d _lastPosition;
	Vec2d _lastScroll;
	Vec2d _position;
	Vec2d _positionDelta;
	Vec2d _savedPosition;
	Vec2d _scroll;
	Vec2d _scrollDelta;
	bool _windowSelected = false;
	bool _cursorHidden   = false;

	CancellableDelegate<MouseButton, InputAction, InputMods> _onButton;
	CancellableDelegate<bool> _onEnter;
	CancellableDelegate<Vec2d> _onMoved;
	CancellableDelegate<Vec2d> _onScroll;
};
}  // namespace Luna
