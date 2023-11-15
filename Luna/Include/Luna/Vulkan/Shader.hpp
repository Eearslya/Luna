#pragma once

#include <Luna/Vulkan/Common.hpp>
#include <Luna/Vulkan/DescriptorSet.hpp>

namespace Luna {
namespace Vulkan {
struct ShaderResourceLayout {
	std::array<DescriptorSetLayout, MaxDescriptorSets> SetLayouts = {};

	uint32_t BindlessSetMask  = 0;
	uint32_t InputMask        = 0;
	uint32_t OutputMask       = 0;
	uint32_t SpecConstantMask = 0;
	uint32_t PushConstantSize = 0;
};

struct ProgramResourceLayout {
	std::array<DescriptorSetLayout, MaxDescriptorSets> SetLayouts = {};

	std::array<std::array<vk::ShaderStageFlags, MaxDescriptorBindings>, MaxDescriptorSets> StagesForBindings = {};
	std::array<vk::ShaderStageFlags, MaxDescriptorSets> StagesForSets                                        = {};

	uint32_t AttributeMask                                  = 0;
	uint32_t BindlessDescriptorSetMask                      = 0;
	uint32_t CombinedSpecConstantMask                       = 0;
	uint32_t DescriptorSetMask                              = 0;
	uint32_t RenderTargetMask                               = 0;
	std::array<uint32_t, ShaderStageCount> SpecConstantMask = {};

	vk::PushConstantRange PushConstantRange = {};
	Hash PushConstantLayoutHash             = {};
};

class PipelineLayout : public HashedObject<PipelineLayout> {
 public:
	PipelineLayout(Hash hash, Device& device, const ProgramResourceLayout& resourceLayout);
	~PipelineLayout() noexcept;

	[[nodiscard]] DescriptorSetAllocator* GetAllocator(uint32_t set) const noexcept {
		return _setAllocators[set];
	}
	[[nodiscard]] vk::PipelineLayout GetPipelineLayout() const noexcept {
		return _pipelineLayout;
	}
	[[nodiscard]] const ProgramResourceLayout& GetResourceLayout() const noexcept {
		return _resourceLayout;
	}
	[[nodiscard]] vk::DescriptorUpdateTemplate GetUpdateTemplate(uint32_t set) const noexcept {
		return _updateTemplates[set];
	}

 private:
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

	[[nodiscard]] ShaderResourceLayout GetResourceLayout() const noexcept {
		return _resourceLayout;
	}
	[[nodiscard]] vk::ShaderModule GetShader() const noexcept {
		return _shaderModule;
	}

 private:
	Device& _device;
	vk::ShaderModule _shaderModule;
	ShaderResourceLayout _resourceLayout;
};

class ProgramBuilder {
 public:
	ProgramBuilder(Device& device);

	ProgramBuilder& Compute(Shader* compute);
	ProgramBuilder& Fragment(Shader* compute);
	ProgramBuilder& Vertex(Shader* compute);

	Program* Build() const;
	void Reset();

 private:
	Device& _device;
	std::array<Shader*, ShaderStageCount> _shaders;
};

class Program : public HashedObject<Program> {
 public:
	Program(Hash hash, Device& device, const std::array<Shader*, ShaderStageCount>& shaders);
	~Program() noexcept;

	[[nodiscard]] const PipelineLayout* GetPipelineLayout() const noexcept {
		return _pipelineLayout;
	}
	[[nodiscard]] const Shader* GetShader(ShaderStage stage) const noexcept {
		return _shaders[int(stage)];
	}

	Pipeline AddPipeline(Hash hash, const Pipeline& pipeline);
	Pipeline GetPipeline(Hash hash) const;
	void PromoteReadWriteToReadOnly();

 private:
	void Bake();

	Device& _device;
	std::array<Shader*, ShaderStageCount> _shaders = {};
	const PipelineLayout* _pipelineLayout          = nullptr;
	VulkanCache<IntrusivePODWrapper<Pipeline>> _pipelines;
};
}  // namespace Vulkan
}  // namespace Luna

template <>
struct std::hash<Luna::Vulkan::ProgramResourceLayout> {
	size_t operator()(const Luna::Vulkan::ProgramResourceLayout& layout) const {
		Luna::Hasher h;

		for (uint32_t i = 0; i < Luna::Vulkan::MaxDescriptorSets; ++i) {
			auto& set = layout.SetLayouts[i];
			h(set.FloatMask);
			h(set.InputAttachmentMask);
			h(set.SampledTexelBufferMask);
			h(set.SampledImageMask);
			h(set.SamplerMask);
			h(set.SeparateImageMask);
			h(set.StorageBufferMask);
			h(set.StorageImageMask);
			h(set.StorageTexelBufferMask);
			h(set.UniformBufferMask);

			for (uint32_t j = 0; j < Luna::Vulkan::MaxDescriptorBindings; ++j) {
				h(set.ArraySizes[j]);
				h(layout.StagesForBindings[i][j]);
			}
		}

		h.Data(sizeof(layout.SpecConstantMask), layout.SpecConstantMask.data());
		h(layout.PushConstantRange.stageFlags);
		h(layout.PushConstantRange.size);
		h(layout.AttributeMask);
		h(layout.RenderTargetMask);

		return static_cast<size_t>(h.Get());
	}
};
