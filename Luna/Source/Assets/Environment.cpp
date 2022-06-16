#include <Luna/Assets/AssetManager.hpp>
#include <Luna/Assets/Environment.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>

namespace Luna {
void EnvironmentDeleter::operator()(Environment* environment) {
	auto manager = AssetManager::Get();
	manager->FreeEnvironment(environment);
}

Environment::Environment() {}

Environment::~Environment() noexcept {}
}  // namespace Luna
