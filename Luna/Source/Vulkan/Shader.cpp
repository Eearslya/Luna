#include <Luna/Core/Log.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Shader.hpp>

namespace Luna {
namespace Vulkan {
Shader::Shader(Hash hash, Device& device, size_t codeSize, const void* code)
		: HashedObject<Shader>(hash), _device(device) {
	Log::Trace("[Vulkan::Shader] Creating new Shader.");

	assert(codeSize % 4 == 0);

	const vk::ShaderModuleCreateInfo shaderCI({}, codeSize, reinterpret_cast<const uint32_t*>(code));
	_shaderModule = _device.GetDevice().createShaderModule(shaderCI);
}

Shader::~Shader() noexcept {
	if (_shaderModule) { _device.GetDevice().destroyShaderModule(_shaderModule); }
}

Program::Program(Hash hash, Device& device, Shader* vertex, Shader* fragment)
		: HashedObject<Program>(hash), _device(device) {
	Log::Trace("[Vulkan::Program] Creating new graphics Program.");

	std::fill(_shaders.begin(), _shaders.end(), nullptr);
	_shaders[static_cast<int>(ShaderStage::Vertex)]   = vertex;
	_shaders[static_cast<int>(ShaderStage::Fragment)] = fragment;
}

Program::Program(Hash hash, Device& device, Shader* compute) : HashedObject<Program>(hash), _device(device) {
	Log::Trace("[Vulkan::Program] Creating new compute Program.");

	std::fill(_shaders.begin(), _shaders.end(), nullptr);
	_shaders[static_cast<int>(ShaderStage::Compute)] = compute;
}

Program::~Program() noexcept {
	if (_pipeline) { _device.GetDevice().destroyPipeline(_pipeline); }
}
}  // namespace Vulkan
}  // namespace Luna
