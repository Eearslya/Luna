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

class PipelineLayout : public HashedObject<PipelineLayout> {
 public:
	PipelineLayout(Hash hash, Device& device, const ProgramResourceLayout& resourceLayout);
	~PipelineLayout() noexcept;

	DescriptorSetAllocator* GetAllocator(uint32_t set) const {
		return _setAllocators[set];
	}
	vk::PipelineLayout GetPipelineLayout() const {
		return _pipelineLayout;
	}
	const ProgramResourceLayout& GetResourceLayout() const {
		return _resourceLayout;
	}
	vk::DescriptorUpdateTemplate GetUpdateTemplate(uint32_t set) const {
		return _updateTemplates[set];
	}

 private:
	void CreateUpdateTemplates();

	Device& _device;
	vk::PipelineLayout _pipelineLayout;
	ProgramResourceLayout _resourceLayout;
	std::array<DescriptorSetAllocator*, MaxDescriptorSets> _setAllocators;
	std::array<vk::DescriptorUpdateTemplate, MaxDescriptorSets> _updateTemplates;
};

class Shader : public HashedObject<Shader> {
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

class ProgramBuilder {
	friend class Device;
	friend class Program;

 public:
	ProgramBuilder();
	ProgramBuilder& AddStage(ShaderStage stage, Shader* shader);

 private:
	std::array<Shader*, ShaderStageCount> _shaders;
};

class Program : public HashedObject<Program> {
 public:
	Program(Hash hash, Device& device, Shader* vertex, Shader* fragment);
	Program(Hash hash, Device& device, Shader* compute);
	Program(Hash hash, Device& device, ProgramBuilder& builder);
	~Program() noexcept;

	PipelineLayout* GetPipelineLayout() const {
		return _pipelineLayout;
	}
	const ProgramResourceLayout& GetResourceLayout() const {
		return _layout;
	}
	Shader* GetShader(ShaderStage stage) const {
		return _shaders[static_cast<int>(stage)];
	}

	vk::Pipeline GetPipeline(Hash hash) const;
	vk::Pipeline AddPipeline(Hash hash, vk::Pipeline pipeline) const;

 private:
	void Bake();

	Device& _device;
	ProgramResourceLayout _layout;
	std::array<Shader*, ShaderStageCount> _shaders = {};
	PipelineLayout* _pipelineLayout                = nullptr;
	mutable VulkanCache<IntrusivePODWrapper<vk::Pipeline>> _pipelines;
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
