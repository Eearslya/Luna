#include <Luna/Assets/Environment.hpp>
#include <Luna/Graphics/AssetManager.hpp>
#include <Luna/Graphics/Graphics.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>

namespace Luna {
void EnvironmentDeleter::operator()(Environment* environment) {
	auto graphics = Graphics::Get();
	auto& manager = graphics->GetAssetManager();
	manager.FreeEnvironment(environment);
}

Environment::Environment() {}

Environment::~Environment() noexcept {}
}  // namespace Luna
