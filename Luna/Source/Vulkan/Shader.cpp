#include <Luna/Utility/BitOps.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Shader.hpp>
#include <spirv_cross.hpp>

namespace Luna {
namespace Vulkan {
static std::string MaskToBindings(uint32_t mask, const uint8_t* arraySizes = nullptr) {
	std::string bindings[32] = {};
	int bindingCount         = 0;

	ForEachBit(mask, [&](uint32_t bit) {
		if (arraySizes && arraySizes[bit]) {
			bindings[bindingCount++] = fmt::format(
				"{}[{}]",
				bit,
				arraySizes[bit] == DescriptorSetLayout::UnsizedArray ? "Bindless" : std::to_string(arraySizes[bit]));
		} else {
			bindings[bindingCount++] = std::to_string(bit);
		}
	});

	return fmt::format("{}", fmt::join(bindings, bindings + bindingCount, ", "));
}

PipelineLayout::PipelineLayout(Hash hash, Device& device, const ProgramResourceLayout& resourceLayout)
		: HashedObject<PipelineLayout>(hash), _device(device), _resourceLayout(resourceLayout) {
	std::array<vk::DescriptorSetLayout, MaxDescriptorSets> layouts = {};

	uint32_t setCount = 0;
	for (uint32_t i = 0; i < MaxDescriptorSets; ++i) {
		_setAllocators[i] =
			_device.RequestDescriptorSetAllocator(_resourceLayout.SetLayouts[i], _resourceLayout.StagesForBindings[i].data());
		layouts[i] = _setAllocators[i]->GetSetLayout();
		if (_resourceLayout.DescriptorSetMask & (1u << i)) { setCount = i + 1; }
	}

	const vk::PipelineLayoutCreateInfo layoutCI(
		{},
		setCount,
		layouts.data(),
		_resourceLayout.PushConstantRange.stageFlags ? 1 : 0,
		_resourceLayout.PushConstantRange.stageFlags ? &_resourceLayout.PushConstantRange : nullptr);
	_pipelineLayout = _device.GetDevice().createPipelineLayout(layoutCI);
	Log::Trace("Vulkan", "Pipeline Layout created.");

	for (uint32_t set = 0; set < MaxDescriptorSets; ++set) {
		if ((_resourceLayout.DescriptorSetMask & (1u << set)) == 0) { continue; }
		if ((_resourceLayout.BindlessDescriptorSetMask & (1u << set)) != 0) { continue; }

		const auto& setLayout = _resourceLayout.SetLayouts[set];

		uint32_t updateCount = 0;
		std::array<vk::DescriptorUpdateTemplateEntry, MaxDescriptorBindings> updateEntries;

		ForEachBit(setLayout.UniformBufferMask, [&](uint32_t binding) {
			updateEntries[updateCount++] =
				vk::DescriptorUpdateTemplateEntry(binding,
			                                    0,
			                                    setLayout.ArraySizes[binding],
			                                    vk::DescriptorType::eUniformBufferDynamic,
			                                    offsetof(ResourceBinding, Buffer) + sizeof(ResourceBinding) * binding,
			                                    sizeof(ResourceBinding));
		});

		ForEachBit(setLayout.StorageBufferMask, [&](uint32_t binding) {
			updateEntries[updateCount++] =
				vk::DescriptorUpdateTemplateEntry(binding,
			                                    0,
			                                    setLayout.ArraySizes[binding],
			                                    vk::DescriptorType::eStorageBuffer,
			                                    offsetof(ResourceBinding, Buffer) + sizeof(ResourceBinding) * binding,
			                                    sizeof(ResourceBinding));
		});

		ForEachBit(setLayout.SampledTexelBufferMask, [&](uint32_t binding) {
			updateEntries[updateCount++] =
				vk::DescriptorUpdateTemplateEntry(binding,
			                                    0,
			                                    setLayout.ArraySizes[binding],
			                                    vk::DescriptorType::eUniformTexelBuffer,
			                                    offsetof(ResourceBinding, BufferView) + sizeof(ResourceBinding) * binding,
			                                    sizeof(ResourceBinding));
		});

		ForEachBit(setLayout.StorageTexelBufferMask, [&](uint32_t binding) {
			updateEntries[updateCount++] =
				vk::DescriptorUpdateTemplateEntry(binding,
			                                    0,
			                                    setLayout.ArraySizes[binding],
			                                    vk::DescriptorType::eStorageTexelBuffer,
			                                    offsetof(ResourceBinding, BufferView) + sizeof(ResourceBinding) * binding,
			                                    sizeof(ResourceBinding));
		});

		ForEachBit(setLayout.SampledImageMask, [&](uint32_t binding) {
			if (setLayout.ArraySizes[binding] == DescriptorSetLayout::UnsizedArray) { return; }

			updateEntries[updateCount++] = vk::DescriptorUpdateTemplateEntry(
				binding,
				0,
				setLayout.ArraySizes[binding],
				vk::DescriptorType::eCombinedImageSampler,
				(setLayout.FloatMask & (1u << binding) ? offsetof(ResourceBinding, Image.Float)
			                                         : offsetof(ResourceBinding, Image.Integer)) +
					sizeof(ResourceBinding) * binding,
				sizeof(ResourceBinding));
		});

		ForEachBit(setLayout.SeparateImageMask, [&](uint32_t binding) {
			updateEntries[updateCount++] = vk::DescriptorUpdateTemplateEntry(
				binding,
				0,
				setLayout.ArraySizes[binding],
				vk::DescriptorType::eSampledImage,
				(setLayout.FloatMask & (1u << binding) ? offsetof(ResourceBinding, Image.Float)
			                                         : offsetof(ResourceBinding, Image.Integer)) +
					sizeof(ResourceBinding) * binding,
				sizeof(ResourceBinding));
		});

		ForEachBit(setLayout.SamplerMask, [&](uint32_t binding) {
			updateEntries[updateCount++] =
				vk::DescriptorUpdateTemplateEntry(binding,
			                                    0,
			                                    setLayout.ArraySizes[binding],
			                                    vk::DescriptorType::eSampler,
			                                    offsetof(ResourceBinding, Image.Float) + sizeof(ResourceBinding) * binding,
			                                    sizeof(ResourceBinding));
		});

		ForEachBit(setLayout.StorageImageMask, [&](uint32_t binding) {
			updateEntries[updateCount++] = vk::DescriptorUpdateTemplateEntry(
				binding,
				0,
				setLayout.ArraySizes[binding],
				vk::DescriptorType::eStorageImage,
				(setLayout.FloatMask & (1u << binding) ? offsetof(ResourceBinding, Image.Float)
			                                         : offsetof(ResourceBinding, Image.Integer)) +
					sizeof(ResourceBinding) * binding,
				sizeof(ResourceBinding));
		});

		ForEachBit(setLayout.InputAttachmentMask, [&](uint32_t binding) {
			updateEntries[updateCount++] = vk::DescriptorUpdateTemplateEntry(
				binding,
				0,
				setLayout.ArraySizes[binding],
				vk::DescriptorType::eInputAttachment,
				(setLayout.FloatMask & (1u << binding) ? offsetof(ResourceBinding, Image.Float)
			                                         : offsetof(ResourceBinding, Image.Integer)) +
					sizeof(ResourceBinding) * binding,
				sizeof(ResourceBinding));
		});

		vk::DescriptorUpdateTemplateCreateInfo templateCI(
			{},
			updateCount,
			updateEntries.data(),
			vk::DescriptorUpdateTemplateType::eDescriptorSet,
			_setAllocators[set]->GetSetLayout(),
			(_resourceLayout.StagesForSets[set] & vk::ShaderStageFlagBits::eCompute) ? vk::PipelineBindPoint::eCompute
																																							 : vk::PipelineBindPoint::eGraphics,
			_pipelineLayout,
			set);
		_updateTemplates[set] = _device.GetDevice().createDescriptorUpdateTemplate(templateCI);
		Log::Trace("Vulkan", "Descriptor Update Template created.");
	}
}

PipelineLayout::~PipelineLayout() noexcept {
	if (_pipelineLayout) { _device.GetDevice().destroyPipelineLayout(_pipelineLayout); }
	for (auto& temp : _updateTemplates) {
		if (temp) { _device.GetDevice().destroyDescriptorUpdateTemplate(temp); }
	}
}

Shader::Shader(Hash hash, Device& device, size_t codeSize, const void* code)
		: HashedObject<Shader>(hash), _device(device) {
	const vk::ShaderModuleCreateInfo shaderCI({}, codeSize, reinterpret_cast<const uint32_t*>(code));
	_shaderModule = _device.GetDevice().createShaderModule(shaderCI);
	Log::Trace("Vulkan", "Shader Module created.");

	// Reflect the SPIR-V code and find the shader's resources.
	const auto UpdateArrayInfo = [&](const spirv_cross::SPIRType& type, uint32_t set, uint32_t binding) {
		auto& size = _resourceLayout.SetLayouts[set].ArraySizes[binding];
		if (!type.array.empty()) {
			if (type.array.size() != 1) {
				Log::Error("Vulkan::Shader", "Reflection error: Array dimension must be 1.");
			} else if (!type.array_size_literal.front()) {
				Log::Error("Vulkan::Shader", "Reflection error: Array dimension must be a literal.");
			} else {
				if (type.array.front() == 0) {
					if (_resourceLayout.BindlessSetMask & (1u << set) && size != DescriptorSetLayout::UnsizedArray) {
						Log::Error("Vulkan::Shader", "Reflection error: Bindless descriptor must be the last descriptor in a set.");
					}

					if (type.basetype != spirv_cross::SPIRType::SampledImage || type.image.dim == spv::DimBuffer) {
						Log::Error("Vulkan::Shader", "Reflection error: Bindless can only be used for combined image samplers.");
					} else {
						_resourceLayout.BindlessSetMask |= 1u << set;
					}

					size = DescriptorSetLayout::UnsizedArray;
				} else if (size && size != type.array.front()) {
					Log::Error("Vulkan::Shader",
					           "Reflection error: Array dimension for set {}, binding {} is inconsistent.",
					           set,
					           binding);
				} else if (type.array.front() + binding > MaxDescriptorBindings) {
					Log::Error("Vulkan::Shader", "Reflection error: Array will go out of bounds.");
				} else {
					size = static_cast<uint8_t>(type.array.front());
				}
			}
		} else {
			if (size && size != 1) {
				Log::Error(
					"Vulkan::Shader", "Reflection error: Array dimension for set {}, binding {} is inconsistent.", set, binding);
			}

			size = 1;
		}
	};

	spirv_cross::Compiler compiler(reinterpret_cast<const uint32_t*>(code), codeSize / sizeof(uint32_t));
	const auto resources = compiler.get_shader_resources();

	for (const auto& image : resources.sampled_images) {
		const auto set     = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
		const auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
		const auto& type   = compiler.get_type(image.type_id);

		if (type.image.dim == spv::DimBuffer) {
			_resourceLayout.SetLayouts[set].SampledTexelBufferMask |= 1u << binding;
		} else {
			_resourceLayout.SetLayouts[set].SampledImageMask |= 1u << binding;
		}

		if (compiler.get_type(type.image.type).basetype == spirv_cross::SPIRType::BaseType::Float) {
			_resourceLayout.SetLayouts[set].FloatMask |= 1u << binding;
		}

		UpdateArrayInfo(type, set, binding);
	}

	for (const auto& image : resources.subpass_inputs) {
		const auto set     = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
		const auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
		const auto& type   = compiler.get_type(image.type_id);

		_resourceLayout.SetLayouts[set].InputAttachmentMask |= 1u << binding;

		if (compiler.get_type(type.image.type).basetype == spirv_cross::SPIRType::BaseType::Float) {
			_resourceLayout.SetLayouts[set].FloatMask |= 1u << binding;
		}

		UpdateArrayInfo(type, set, binding);
	}

	for (const auto& image : resources.separate_images) {
		const auto set     = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
		const auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
		const auto& type   = compiler.get_type(image.type_id);

		if (type.image.dim == spv::DimBuffer) {
			_resourceLayout.SetLayouts[set].SampledTexelBufferMask |= 1u << binding;
		} else {
			_resourceLayout.SetLayouts[set].SeparateImageMask |= 1u << binding;
		}

		if (compiler.get_type(type.image.type).basetype == spirv_cross::SPIRType::BaseType::Float) {
			_resourceLayout.SetLayouts[set].FloatMask |= 1u << binding;
		}

		UpdateArrayInfo(type, set, binding);
	}

	for (const auto& image : resources.storage_images) {
		const auto set     = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
		const auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
		const auto& type   = compiler.get_type(image.type_id);

		_resourceLayout.SetLayouts[set].StorageImageMask |= 1u << binding;

		if (compiler.get_type(type.image.type).basetype == spirv_cross::SPIRType::BaseType::Float) {
			_resourceLayout.SetLayouts[set].FloatMask |= 1u << binding;
		}

		UpdateArrayInfo(type, set, binding);
	}

	for (const auto& sampler : resources.separate_samplers) {
		const auto set     = compiler.get_decoration(sampler.id, spv::DecorationDescriptorSet);
		const auto binding = compiler.get_decoration(sampler.id, spv::DecorationBinding);
		const auto& type   = compiler.get_type(sampler.type_id);

		_resourceLayout.SetLayouts[set].SamplerMask |= 1u << binding;

		UpdateArrayInfo(type, set, binding);
	}

	for (const auto& buffer : resources.uniform_buffers) {
		const auto set     = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
		const auto binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
		const auto& type   = compiler.get_type(buffer.type_id);

		_resourceLayout.SetLayouts[set].UniformBufferMask |= 1u << binding;

		UpdateArrayInfo(type, set, binding);
	}

	for (const auto& buffer : resources.storage_buffers) {
		const auto set     = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
		const auto binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
		const auto& type   = compiler.get_type(buffer.type_id);

		_resourceLayout.SetLayouts[set].StorageBufferMask |= 1u << binding;

		UpdateArrayInfo(type, set, binding);
	}

	for (const auto& attribute : resources.stage_inputs) {
		const auto location = compiler.get_decoration(attribute.id, spv::DecorationLocation);

		_resourceLayout.InputMask |= 1u << location;
	}

	for (const auto& attribute : resources.stage_outputs) {
		const auto location = compiler.get_decoration(attribute.id, spv::DecorationLocation);

		_resourceLayout.OutputMask |= 1u << location;
	}

	if (!resources.push_constant_buffers.empty()) {
		_resourceLayout.PushConstantSize =
			compiler.get_declared_struct_size(compiler.get_type(resources.push_constant_buffers.front().base_type_id));
	}

	for (const auto& constant : compiler.get_specialization_constants()) {
		if (constant.constant_id >= MaxSpecConstants) {
			Log::Error("Vulkan::Shader",
			           "Reflection error: Specialization constant {} is out of range and will be ignored. Max allowed is {}.",
			           constant.constant_id,
			           MaxSpecConstants);
			continue;
		}

		_resourceLayout.SpecConstantMask |= 1u << constant.constant_id;
	}

	// Dump shader resources to console.
	Log::Trace("Vulkan::Shader", "- Shader Resources:");

	for (int i = 0; i < MaxDescriptorSets; ++i) {
		const auto& set = _resourceLayout.SetLayouts[i];

		if ((set.FloatMask | set.InputAttachmentMask | set.StorageTexelBufferMask | set.SampledImageMask | set.SamplerMask |
		     set.SeparateImageMask | set.StorageBufferMask | set.StorageImageMask | set.UniformBufferMask) == 0) {
			continue;
		}

		Log::Trace("Vulkan::Shader", "    Descriptor Set {}:", i);
		if (set.FloatMask) {
			Log::Trace("Vulkan::Shader", "      Floating Point Images: {}", MaskToBindings(set.FloatMask, set.ArraySizes));
		}
		if (set.InputAttachmentMask) {
			Log::Trace(
				"Vulkan::Shader", "      Input Attachments: {}", MaskToBindings(set.InputAttachmentMask, set.ArraySizes));
		}
		if (set.StorageTexelBufferMask) {
			Log::Trace(
				"Vulkan::Shader", "      Texel Buffers: {}", MaskToBindings(set.StorageTexelBufferMask, set.ArraySizes));
		}
		if (set.SampledImageMask) {
			Log::Trace("Vulkan::Shader", "      Sampled Images: {}", MaskToBindings(set.SampledImageMask, set.ArraySizes));
		}
		if (set.SamplerMask) {
			Log::Trace("Vulkan::Shader", "      Samplers: {}", MaskToBindings(set.SamplerMask, set.ArraySizes));
		}
		if (set.SeparateImageMask) {
			Log::Trace("Vulkan::Shader", "      Separate Images: {}", MaskToBindings(set.SeparateImageMask, set.ArraySizes));
		}
		if (set.StorageBufferMask) {
			Log::Trace("Vulkan::Shader", "      Storage Buffers: {}", MaskToBindings(set.StorageBufferMask, set.ArraySizes));
		}
		if (set.StorageImageMask) {
			Log::Trace("Vulkan::Shader", "      Storage Images: {}", MaskToBindings(set.StorageImageMask, set.ArraySizes));
		}
		if (set.UniformBufferMask) {
			Log::Trace("Vulkan::Shader", "      Uniform Buffers: {}", MaskToBindings(set.UniformBufferMask, set.ArraySizes));
		}
	}

	if (_resourceLayout.BindlessSetMask) {
		Log::Trace("Vulkan::Shader", "    Bindless Sets: {}", MaskToBindings(_resourceLayout.BindlessSetMask));
	}
	if (_resourceLayout.InputMask) {
		Log::Trace("Vulkan::Shader", "    Attribute Inputs: {}", MaskToBindings(_resourceLayout.InputMask));
	}
	if (_resourceLayout.OutputMask) {
		Log::Trace("Vulkan::Shader", "    Attribute Outputs: {}", MaskToBindings(_resourceLayout.OutputMask));
	}
	if (_resourceLayout.SpecConstantMask) {
		Log::Trace("Vulkan::Shader", "    Specialization Constants: {}", MaskToBindings(_resourceLayout.SpecConstantMask));
	}
	if (_resourceLayout.PushConstantSize) {
		Log::Trace("Vulkan::Shader", "    Push Constant Size: {}B", _resourceLayout.PushConstantSize);
	}
}

Shader::~Shader() noexcept {
	if (_shaderModule) { _device.GetDevice().destroyShaderModule(_shaderModule); }
}

ProgramBuilder::ProgramBuilder(Device& device) : _device(device) {
	_shaders.fill(nullptr);
}

ProgramBuilder& ProgramBuilder::Compute(Shader* compute) {
	_shaders[int(ShaderStage::Compute)] = compute;

	return *this;
}

ProgramBuilder& ProgramBuilder::Fragment(Shader* fragment) {
	_shaders[int(ShaderStage::Fragment)] = fragment;

	return *this;
}

ProgramBuilder& ProgramBuilder::Vertex(Shader* vertex) {
	_shaders[int(ShaderStage::Vertex)] = vertex;

	return *this;
}

Program* ProgramBuilder::Build() const {
	return _device.RequestProgram(_shaders);
}

void ProgramBuilder::Reset() {
	_shaders.fill(nullptr);
}

Program::Program(Hash hash, Device& device, const std::array<Shader*, ShaderStageCount>& shaders)
		: HashedObject<Program>(hash), _device(device), _shaders(shaders) {
	Bake();
}

Program::~Program() noexcept {
	for (auto& pipeline : _pipelines.GetReadOnly()) { _device.GetDevice().destroyPipeline(pipeline.Get().Pipeline); }
	for (auto& pipeline : _pipelines.GetReadWrite()) { _device.GetDevice().destroyPipeline(pipeline.Get().Pipeline); }
}

Pipeline Program::AddPipeline(Hash hash, const Pipeline& pipeline) {
	return _pipelines.EmplaceYield(hash, pipeline)->Get();
}

Pipeline Program::GetPipeline(Hash hash) const {
	auto* ret = _pipelines.Find(hash);

	return ret ? ret->Get() : Pipeline{};
}

void Program::PromoteReadWriteToReadOnly() {
	_pipelines.MoveToReadOnly();
}

void Program::Bake() {
	ProgramResourceLayout resourceLayout = {};

	if (_shaders[int(ShaderStage::Vertex)]) {
		resourceLayout.AttributeMask = _shaders[int(ShaderStage::Vertex)]->GetResourceLayout().InputMask;
	}
	if (_shaders[int(ShaderStage::Fragment)]) {
		resourceLayout.RenderTargetMask = _shaders[int(ShaderStage::Fragment)]->GetResourceLayout().OutputMask;
	}

	for (int i = 0; i < ShaderStageCount; ++i) {
		const auto* shader = _shaders[i];
		if (!shader) { continue; }

		const auto& shaderLayout          = shader->GetResourceLayout();
		vk::ShaderStageFlagBits stageMask = static_cast<vk::ShaderStageFlagBits>(1u << i);

		if (shaderLayout.PushConstantSize) {
			resourceLayout.PushConstantRange.stageFlags |= stageMask;
			resourceLayout.PushConstantRange.size =
				std::max(resourceLayout.PushConstantRange.size, shaderLayout.PushConstantSize);
		}

		resourceLayout.SpecConstantMask[i] = shaderLayout.SpecConstantMask;
		resourceLayout.CombinedSpecConstantMask |= shaderLayout.SpecConstantMask;
		resourceLayout.BindlessDescriptorSetMask |= shaderLayout.BindlessSetMask;

		for (uint32_t set = 0; set < MaxDescriptorSets; ++set) {
			resourceLayout.SetLayouts[set].FloatMask |= shaderLayout.SetLayouts[set].FloatMask;
			resourceLayout.SetLayouts[set].InputAttachmentMask |= shaderLayout.SetLayouts[set].InputAttachmentMask;
			resourceLayout.SetLayouts[set].SampledImageMask |= shaderLayout.SetLayouts[set].SampledImageMask;
			resourceLayout.SetLayouts[set].SampledTexelBufferMask |= shaderLayout.SetLayouts[set].SampledTexelBufferMask;
			resourceLayout.SetLayouts[set].SamplerMask |= shaderLayout.SetLayouts[set].SamplerMask;
			resourceLayout.SetLayouts[set].SeparateImageMask |= shaderLayout.SetLayouts[set].SeparateImageMask;
			resourceLayout.SetLayouts[set].StorageBufferMask |= shaderLayout.SetLayouts[set].StorageBufferMask;
			resourceLayout.SetLayouts[set].StorageImageMask |= shaderLayout.SetLayouts[set].StorageImageMask;
			resourceLayout.SetLayouts[set].StorageTexelBufferMask |= shaderLayout.SetLayouts[set].StorageTexelBufferMask;
			resourceLayout.SetLayouts[set].UniformBufferMask |= shaderLayout.SetLayouts[set].UniformBufferMask;

			const uint32_t activeBinds =
				shaderLayout.SetLayouts[set].InputAttachmentMask | shaderLayout.SetLayouts[set].SampledImageMask |
				shaderLayout.SetLayouts[set].SampledTexelBufferMask | shaderLayout.SetLayouts[set].SamplerMask |
				shaderLayout.SetLayouts[set].SeparateImageMask | shaderLayout.SetLayouts[set].StorageBufferMask |
				shaderLayout.SetLayouts[set].StorageImageMask | shaderLayout.SetLayouts[set].StorageTexelBufferMask |
				shaderLayout.SetLayouts[set].UniformBufferMask;
			if (activeBinds) { resourceLayout.StagesForSets[set] |= stageMask; }

			ForEachBit(activeBinds, [&](uint32_t bit) {
				resourceLayout.StagesForBindings[set][bit] |= stageMask;
				auto& combinedSize     = resourceLayout.SetLayouts[set].ArraySizes[bit];
				const auto& shaderSize = shaderLayout.SetLayouts[set].ArraySizes[bit];
				if (combinedSize && combinedSize != shaderSize) {
					Log::Error("Vulkan::Program",
					           "Reflection error: Mismatched array sizes between shader stages for set {}, binding {}.",
					           set,
					           bit);
				} else {
					combinedSize = shaderSize;
				}
			});
		}
	}

	for (uint32_t set = 0; set < MaxDescriptorSets; ++set) {
		if (resourceLayout.StagesForSets[set]) {
			resourceLayout.DescriptorSetMask |= 1u << set;
			for (uint32_t binding = 0; binding < MaxDescriptorBindings; ++binding) {
				auto& arraySize = resourceLayout.SetLayouts[set].ArraySizes[binding];
				if (arraySize == DescriptorSetLayout::UnsizedArray) {
					resourceLayout.StagesForBindings[set][binding] = vk::ShaderStageFlagBits::eAll;
				} else if (arraySize == 0) {
					arraySize = 1;
				}
			}
		}
	}

	Hasher h;
	h(resourceLayout.PushConstantRange.stageFlags);
	h(resourceLayout.PushConstantRange.size);
	resourceLayout.PushConstantLayoutHash = h.Get();

	_pipelineLayout = _device.RequestPipelineLayout(resourceLayout);

	Log::Trace("Vulkan::Program", "- Program Resources:");

	for (int i = 0; i < MaxDescriptorSets; ++i) {
		const auto& set = resourceLayout.SetLayouts[i];

		if ((set.FloatMask | set.InputAttachmentMask | set.SampledTexelBufferMask | set.SampledImageMask | set.SamplerMask |
		     set.SeparateImageMask | set.StorageBufferMask | set.StorageImageMask | set.UniformBufferMask) == 0) {
			continue;
		}

		Log::Trace("Vulkan::Program", "    Descriptor Set {}:", i);
		Log::Trace("Vulkan::Program",
		           "      Stages: {}",
		           vk::to_string(static_cast<vk::ShaderStageFlags>(resourceLayout.StagesForSets[i])));
		if (set.FloatMask) {
			Log::Trace("Vulkan::Program", "      Floating Point Images: {}", MaskToBindings(set.FloatMask, set.ArraySizes));
		}
		if (set.InputAttachmentMask) {
			Log::Trace(
				"Vulkan::Program", "      Input Attachments: {}", MaskToBindings(set.InputAttachmentMask, set.ArraySizes));
		}
		if (set.SampledTexelBufferMask) {
			Log::Trace(
				"Vulkan::Program", "      Sampled Buffers: {}", MaskToBindings(set.SampledTexelBufferMask, set.ArraySizes));
		}
		if (set.SampledImageMask) {
			Log::Trace("Vulkan::Program", "      Sampled Images: {}", MaskToBindings(set.SampledImageMask, set.ArraySizes));
		}
		if (set.SamplerMask) {
			Log::Trace("Vulkan::Program", "      Samplers: {}", MaskToBindings(set.SamplerMask, set.ArraySizes));
		}
		if (set.SeparateImageMask) {
			Log::Trace("Vulkan::Program", "      Separate Images: {}", MaskToBindings(set.SeparateImageMask, set.ArraySizes));
		}
		if (set.StorageBufferMask) {
			Log::Trace("Vulkan::Program", "      Storage Buffers: {}", MaskToBindings(set.StorageBufferMask, set.ArraySizes));
		}
		if (set.StorageImageMask) {
			Log::Trace("Vulkan::Program", "      Storage Images: {}", MaskToBindings(set.StorageImageMask, set.ArraySizes));
		}
		if (set.UniformBufferMask) {
			Log::Trace("Vulkan::Program", "      Uniform Buffers: {}", MaskToBindings(set.UniformBufferMask, set.ArraySizes));
		}
	}

	if (resourceLayout.AttributeMask) {
		Log::Trace("Vulkan::Program", "    Input Attributes: {}", MaskToBindings(resourceLayout.AttributeMask));
	}
	if (resourceLayout.BindlessDescriptorSetMask) {
		Log::Trace("Vulkan::Program", "    Bindless Sets: {}", MaskToBindings(resourceLayout.BindlessDescriptorSetMask));
	}
	if (resourceLayout.CombinedSpecConstantMask) {
		Log::Trace(
			"Vulkan::Program", "    Specialization Constants: {}", MaskToBindings(resourceLayout.CombinedSpecConstantMask));
		for (uint32_t stage = 0; stage < ShaderStageCount; ++stage) {
			if (resourceLayout.SpecConstantMask[stage]) {
				Log::Trace("Vulkan::Program",
				           "      {}: {}",
				           vk::to_string(static_cast<vk::ShaderStageFlagBits>(stage)),
				           MaskToBindings(resourceLayout.SpecConstantMask[stage]));
			}
		}
	}
	if (resourceLayout.DescriptorSetMask) {
		Log::Trace("Vulkan::Program", "    Descriptor Sets: {}", MaskToBindings(resourceLayout.DescriptorSetMask));
	}
	if (resourceLayout.RenderTargetMask) {
		Log::Trace("Vulkan::Program", "    Render Targets: {}", MaskToBindings(resourceLayout.RenderTargetMask));
	}
	if (resourceLayout.PushConstantRange.size) {
		Log::Trace("Vulkan::Program",
		           "    Push Constant: {}B in {}",
		           resourceLayout.PushConstantRange.size,
		           vk::to_string(resourceLayout.PushConstantRange.stageFlags));
	}
}
}  // namespace Vulkan
}  // namespace Luna
