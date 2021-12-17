#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
class Shader : public HashedObject<Shader>, NonCopyable {
 public:
	Shader(Hash hash, Device& device, size_t codeSize, const void* code);
	~Shader() noexcept;

	vk::ShaderModule GetShaderModule() const {
		return _shaderModule;
	}

 private:
	Device& _device;
	vk::ShaderModule _shaderModule;
};

class Program : public HashedObject<Program>, NonCopyable {
 public:
	Program(Hash hash, Device& device, Shader* vertex, Shader* fragment);
	Program(Hash hash, Device& device, Shader* compute);
	~Program() noexcept;

	vk::Pipeline GetPipeline() const {
		return _pipeline;
	}
	Shader* GetShader(ShaderStage stage) const {
		return _shaders[static_cast<int>(stage)];
	}

	void SetPipeline(vk::Pipeline pipeline) const {
		_pipeline = pipeline;
	}

 private:
	Device& _device;
	std::array<Shader*, ShaderStageCount> _shaders = {};
	mutable vk::Pipeline _pipeline;
};
}  // namespace Vulkan
}  // namespace Luna
