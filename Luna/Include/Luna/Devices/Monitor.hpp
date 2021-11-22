#pragma once

#include <Luna/Math/Vec2.hpp>
#include <string>
#include <vector>

struct GLFWmonitor;

namespace Luna {
struct VideoMode {
	int32_t Width;
	int32_t Height;
	int32_t RedBits;
	int32_t GreenBits;
	int32_t BlueBits;
	int32_t RefreshRate;
};

struct GammaRamp {
	uint16_t* Red   = nullptr;
	uint16_t* Green = nullptr;
	uint16_t* Blue  = nullptr;
	uint32_t Size   = 0;
};

class Monitor {
	friend class Window;

 public:
	explicit Monitor(GLFWmonitor* monitor = nullptr);

	GLFWmonitor* GetMonitor() const {
		return _monitor;
	}

	Vec2f GetContentScale() const;
	GammaRamp GetGammaRamp() const;
	std::string GetName() const;
	Vec2ui GetPosition() const;
	Vec2ui GetSize() const;
	Vec2ui GetWorkareaPosition() const;
	Vec2ui GetWorkareaSize() const;
	VideoMode GetVideoMode() const;
	std::vector<VideoMode> GetVideoModes() const;
	bool IsPrimary() const;

	void SetGammaRamp(const GammaRamp& ramp) const;

 private:
	GLFWmonitor* _monitor = nullptr;
};
}  // namespace Luna
