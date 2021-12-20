#pragma once

#include <Luna/Vulkan/Common.hpp>
#include <Luna/Vulkan/DescriptorSet.hpp>

namespace Luna {
namespace Vulkan {
struct ShaderResourceLayout {
	DescriptorSetLayout SetLayouts[MaxDescriptorSets] = {};
	uint32_t BindlessSetMask                          = 0;
	uint32_t InputMask                                = 0;
	uint32_t OutputMask                               = 0;
	uint32_t SpecConstantMask                         = 0;
	uint32_t PushConstantSize                         = 0;
};

struct ProgramResourceLayout {
	DescriptorSetLayout SetLayouts[MaxDescriptorSets]                    = {};
	uint32_t AttributeMask                                               = 0;
	uint32_t BindlessDescriptorSetMask                                   = 0;
	uint32_t CombinedSpecConstantMask                                    = 0;
	uint32_t DescriptorSetMask                                           = 0;
	uint32_t RenderTargetMask                                            = 0;
	uint32_t SpecConstantMask[ShaderStageCount]                          = {};
	uint32_t StagesForBindings[MaxDescriptorSets][MaxDescriptorBindings] = {};
	uint32_t StagesForSets[MaxDescriptorSets]                            = {};
	vk::PushConstantRange PushConstantRange                              = {};
	Hash PushConstantLayoutHash                                          = {};
};

class PipelineLayout : public HashedObject<PipelineLayout>, NonCopyable {
 public:
	PipelineLayout(Hash hash, Device& device, const ProgramResourceLayout& resourceLayout);
	~PipelineLayout() noexcept;

	vk::PipelineLayout GetPipelineLayout() const {
		return _pipelineLayout;
	}
	const ProgramResourceLayout& GetResourceLayout() const {
		return _resourceLayout;
	}

 private:
	Device& _device;
	vk::PipelineLayout _pipelineLayout;
	ProgramResourceLayout _resourceLayout;
	std::array<DescriptorSetAllocator*, MaxDescriptorSets> _setAllocators;
};

class Shader : public HashedObject<Shader>, NonCopyable {
 public:
	Shader(Hash hash, Device& device, size_t codeSize, const void* code);
	~Shader() noexcept;

	const ShaderResourceLayout& GetResourceLayout() const {
		return _layout;
	}
	vk::ShaderModule GetShaderModule() const {
		return _shaderModule;
	}

 private:
	Device& _device;
	vk::ShaderModule _shaderModule;
	ShaderResourceLayout _layout;
};

class Program : public HashedObject<Program>, NonCopyable {
 public:
	Program(Hash hash, Device& device, Shader* vertex, Shader* fragment);
	Program(Hash hash, Device& device, Shader* compute);
	~Program() noexcept;

	vk::Pipeline GetPipeline() const {
		return _pipeline;
	}
	PipelineLayout* GetPipelineLayout() const {
		return _pipelineLayout;
	}
	const ProgramResourceLayout& GetResourceLayout() const {
		return _layout;
	}
	Shader* GetShader(ShaderStage stage) const {
		return _shaders[static_cast<int>(stage)];
	}

	void SetPipeline(vk::Pipeline pipeline) const {
		_pipeline = pipeline;
	}

 private:
	void Bake();

	Device& _device;
	ProgramResourceLayout _layout;
	std::array<Shader*, ShaderStageCount> _shaders = {};
	mutable vk::Pipeline _pipeline;
	PipelineLayout* _pipelineLayout = nullptr;
};
}  // namespace Vulkan
}  // namespace Luna

template <>
struct std::hash<Luna::Vulkan::ProgramResourceLayout> {
	size_t operator()(const Luna::Vulkan::ProgramResourceLayout& layout) {
		Luna::Hasher h;

		h.Data(sizeof(layout.SetLayouts), layout.SetLayouts);
		h.Data(sizeof(layout.StagesForBindings), layout.StagesForBindings);
		h.Data(sizeof(layout.SpecConstantMask), layout.SpecConstantMask);
		h(layout.PushConstantRange.stageFlags);
		h(layout.PushConstantRange.size);
		h(layout.AttributeMask);
		h(layout.RenderTargetMask);

		return static_cast<size_t>(h.Get());
	}
};
