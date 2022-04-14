#include <GLFW/glfw3.h>

#include <Luna/Devices/Monitor.hpp>

namespace Luna {
Monitor::Monitor(GLFWmonitor* monitor) : _monitor(monitor) {}

glm::vec2 Monitor::GetContentScale() const {
	float x, y;
	glfwGetMonitorContentScale(_monitor, &x, &y);
	return {x, y};
}

GammaRamp Monitor::GetGammaRamp() const {
	const auto ramp = glfwGetGammaRamp(_monitor);

	return *reinterpret_cast<const GammaRamp*>(&ramp);
}

std::string Monitor::GetName() const {
	return glfwGetMonitorName(_monitor);
}

glm::uvec2 Monitor::GetPosition() const {
	int32_t x, y;
	glfwGetMonitorPos(_monitor, &x, &y);
	return {x, y};
}

glm::uvec2 Monitor::GetSize() const {
	int32_t w, h;
	glfwGetMonitorPhysicalSize(_monitor, &w, &h);
	return {w, h};
}

glm::uvec2 Monitor::GetWorkareaPosition() const {
	int32_t x, y;
	glfwGetMonitorWorkarea(_monitor, &x, &y, nullptr, nullptr);
	return {x, y};
}

glm::uvec2 Monitor::GetWorkareaSize() const {
	int32_t w, h;
	glfwGetMonitorWorkarea(_monitor, nullptr, nullptr, &w, &h);
	return {w, h};
}

VideoMode Monitor::GetVideoMode() const {
	const auto videoMode = glfwGetVideoMode(_monitor);

	return *reinterpret_cast<const VideoMode*>(videoMode);
}

std::vector<VideoMode> Monitor::GetVideoModes() const {
	int32_t videoModeCount = 0;
	auto videoModes        = glfwGetVideoModes(_monitor, &videoModeCount);
	std::vector<VideoMode> modes(static_cast<uint32_t>(videoModeCount));
	for (uint32_t i = 0; i < videoModeCount; ++i) { modes[i] = *reinterpret_cast<const VideoMode*>(&videoModes[i]); }

	return modes;
}

bool Monitor::IsPrimary() const {
	return _monitor == glfwGetPrimaryMonitor();
}

void Monitor::SetGammaRamp(const GammaRamp& ramp) const {
	const auto gammaRamp = reinterpret_cast<const GLFWgammaramp*>(&ramp);
	glfwSetGammaRamp(_monitor, gammaRamp);
}
}  // namespace Luna
