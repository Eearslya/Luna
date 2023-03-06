#include <Luna/Utility/BitOps.hpp>
#include <Luna/Utility/Log.hpp>
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
			_device.RequestDescriptorSetAllocator(resourceLayout.SetLayouts[i], resourceLayout.StagesForBindings[i]);
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
	Log::Debug("Vulkan", "Pipeline Layout created.");

	CreateUpdateTemplates();
}

PipelineLayout::~PipelineLayout() noexcept {
	if (_pipelineLayout) { _device.GetDevice().destroyPipelineLayout(_pipelineLayout); }
	for (auto& temp : _updateTemplates) {
		if (temp) { _device.GetDevice().destroyDescriptorUpdateTemplate(temp); }
	}
}

void PipelineLayout::CreateUpdateTemplates() {
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

		ForEachBit(setLayout.SamplerMask & ~setLayout.ImmutableSamplerMask, [&](uint32_t binding) {
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
			(_resourceLayout.StagesForSets[set] & uint32_t(vk::ShaderStageFlagBits::eCompute))
				? vk::PipelineBindPoint::eCompute
				: vk::PipelineBindPoint::eGraphics,
			_pipelineLayout,
			set);
		_updateTemplates[set] = _device.GetDevice().createDescriptorUpdateTemplate(templateCI);
		Log::Debug("Vulkan", "Descriptor Update Template created.");
	}
}

Shader::Shader(Hash hash, Device& device, size_t codeSize, const void* code)
		: HashedObject<Shader>(hash), _device(device) {
	const vk::ShaderModuleCreateInfo shaderCI({}, codeSize, reinterpret_cast<const uint32_t*>(code));
	_shaderModule = _device.GetDevice().createShaderModule(shaderCI);
	Log::Debug("Vulkan", "Shader Module created.");

	// Reflect the SPIR-V code and find the shader's resources.
	{
		const auto UpdateArrayInfo = [&](const spirv_cross::SPIRType& type, uint32_t set, uint32_t binding) {
			auto& size = _layout.SetLayouts[set].ArraySizes[binding];
			if (!type.array.empty()) {
				if (type.array.size() != 1) {
					Log::Error("Vulkan::Shader", "Reflection error: Array dimension must be 1.");
				} else if (!type.array_size_literal.front()) {
					Log::Error("Vulkan::Shader", "Reflection error: Array dimension must be a literal.");
				} else {
					if (type.array.front() == 0) {
						if (binding != 0) {
							Log::Error("Vulkan::Shader", "Reflection error: Bindless textures can only be used with binding 0.");
						}

						if (type.basetype != spirv_cross::SPIRType::Image || type.image.dim == spv::DimBuffer) {
							Log::Error("Vulkan::Shader", "Reflection error: Bindless can only be used for sampled images.");
						} else {
							_layout.BindlessSetMask |= 1u << set;
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
					Log::Error("Vulkan::Shader",
					           "Reflection error: Array dimension for set {}, binding {} is inconsistent.",
					           set,
					           binding);
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
				_layout.SetLayouts[set].SampledBufferMask |= 1u << binding;
			} else {
				_layout.SetLayouts[set].SampledImageMask |= 1u << binding;
			}

			if (compiler.get_type(type.image.type).basetype == spirv_cross::SPIRType::BaseType::Float) {
				_layout.SetLayouts[set].FloatMask |= 1u << binding;
			}

			UpdateArrayInfo(type, set, binding);
		}

		for (const auto& image : resources.subpass_inputs) {
			const auto set     = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
			const auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
			const auto& type   = compiler.get_type(image.type_id);

			_layout.SetLayouts[set].InputAttachmentMask |= 1u << binding;

			if (compiler.get_type(type.image.type).basetype == spirv_cross::SPIRType::BaseType::Float) {
				_layout.SetLayouts[set].FloatMask |= 1u << binding;
			}

			UpdateArrayInfo(type, set, binding);
		}

		for (const auto& image : resources.separate_images) {
			const auto set     = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
			const auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
			const auto& type   = compiler.get_type(image.type_id);

			if (type.image.dim == spv::DimBuffer) {
				_layout.SetLayouts[set].SampledBufferMask |= 1u << binding;
			} else {
				_layout.SetLayouts[set].SeparateImageMask |= 1u << binding;
			}

			if (compiler.get_type(type.image.type).basetype == spirv_cross::SPIRType::BaseType::Float) {
				_layout.SetLayouts[set].FloatMask |= 1u << binding;
			}

			UpdateArrayInfo(type, set, binding);
		}

		for (const auto& sampler : resources.separate_samplers) {
			const auto set     = compiler.get_decoration(sampler.id, spv::DecorationDescriptorSet);
			const auto binding = compiler.get_decoration(sampler.id, spv::DecorationBinding);
			const auto& type   = compiler.get_type(sampler.type_id);

			_layout.SetLayouts[set].SamplerMask |= 1u << binding;

			UpdateArrayInfo(type, set, binding);
		}

		for (const auto& image : resources.storage_images) {
			const auto set     = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
			const auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
			const auto& type   = compiler.get_type(image.type_id);

			_layout.SetLayouts[set].StorageImageMask |= 1u << binding;

			if (compiler.get_type(type.image.type).basetype == spirv_cross::SPIRType::BaseType::Float) {
				_layout.SetLayouts[set].FloatMask |= 1u << binding;
			}

			UpdateArrayInfo(type, set, binding);
		}

		for (const auto& buffer : resources.uniform_buffers) {
			const auto set     = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
			const auto binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
			const auto& type   = compiler.get_type(buffer.type_id);

			_layout.SetLayouts[set].UniformBufferMask |= 1u << binding;

			UpdateArrayInfo(type, set, binding);
		}

		for (const auto& buffer : resources.storage_buffers) {
			const auto set     = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
			const auto binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
			const auto& type   = compiler.get_type(buffer.type_id);

			_layout.SetLayouts[set].StorageBufferMask |= 1u << binding;

			UpdateArrayInfo(type, set, binding);
		}

		for (const auto& attribute : resources.stage_inputs) {
			const auto location = compiler.get_decoration(attribute.id, spv::DecorationLocation);

			_layout.InputMask |= 1u << location;
		}

		for (const auto& attribute : resources.stage_outputs) {
			const auto location = compiler.get_decoration(attribute.id, spv::DecorationLocation);

			_layout.OutputMask |= 1u << location;
		}

		if (!resources.push_constant_buffers.empty()) {
			_layout.PushConstantSize =
				compiler.get_declared_struct_size(compiler.get_type(resources.push_constant_buffers.front().base_type_id));
		}

		for (const auto& constant : compiler.get_specialization_constants()) {
			if (constant.constant_id >= MaxSpecConstants) {
				Log::Error("Vulkan::Shader",
				           "Reflection error: Specialization constant {} is out of range and will be ignored. "
				           "Max allowed is {}.",
				           constant.constant_id,
				           MaxSpecConstants);
				continue;
			}

			_layout.SpecConstantMask |= 1u << constant.constant_id;
		}
	}

// Dump shader resources to console.
#if 1
	{
		Log::Trace("Vulkan::Shader", "- Shader Resources:");

		for (int i = 0; i < MaxDescriptorSets; ++i) {
			const auto& set = _layout.SetLayouts[i];

			if ((set.FloatMask | set.ImmutableSamplerMask | set.InputAttachmentMask | set.SampledBufferMask |
			     set.SampledImageMask | set.SamplerMask | set.SeparateImageMask | set.StorageBufferMask |
			     set.StorageImageMask | set.UniformBufferMask) == 0) {
				continue;
			}

			Log::Trace("Vulkan::Shader", "    Descriptor Set {}:", i);
			if (set.FloatMask) {
				Log::Trace("Vulkan::Shader", "      Floating Point Images: {}", MaskToBindings(set.FloatMask, set.ArraySizes));
			}
			if (set.ImmutableSamplerMask) {
				Log::Trace(
					"Vulkan::Shader", "      Immutable Samplers: {}", MaskToBindings(set.ImmutableSamplerMask, set.ArraySizes));
			}
			if (set.InputAttachmentMask) {
				Log::Trace(
					"Vulkan::Shader", "      Input Attachments: {}", MaskToBindings(set.InputAttachmentMask, set.ArraySizes));
			}
			if (set.SampledBufferMask) {
				Log::Trace(
					"Vulkan::Shader", "      Sampled Buffers: {}", MaskToBindings(set.SampledBufferMask, set.ArraySizes));
			}
			if (set.SampledImageMask) {
				Log::Trace("Vulkan::Shader", "      Sampled Images: {}", MaskToBindings(set.SampledImageMask, set.ArraySizes));
			}
			if (set.SamplerMask) {
				Log::Trace("Vulkan::Shader", "      Samplers: {}", MaskToBindings(set.SamplerMask, set.ArraySizes));
			}
			if (set.SeparateImageMask) {
				Log::Trace(
					"Vulkan::Shader", "      Separate Images: {}", MaskToBindings(set.SeparateImageMask, set.ArraySizes));
			}
			if (set.StorageBufferMask) {
				Log::Trace(
					"Vulkan::Shader", "      Storage Buffers: {}", MaskToBindings(set.StorageBufferMask, set.ArraySizes));
			}
			if (set.StorageImageMask) {
				Log::Trace("Vulkan::Shader", "      Storage Images: {}", MaskToBindings(set.StorageImageMask, set.ArraySizes));
			}
			if (set.UniformBufferMask) {
				Log::Trace(
					"Vulkan::Shader", "      Uniform Buffers: {}", MaskToBindings(set.UniformBufferMask, set.ArraySizes));
			}
		}

		if (_layout.BindlessSetMask) {
			Log::Trace("Vulkan::Shader", "    Bindless Sets: {}", MaskToBindings(_layout.BindlessSetMask));
		}
		if (_layout.InputMask) {
			Log::Trace("Vulkan::Shader", "    Attribute Inputs: {}", MaskToBindings(_layout.InputMask));
		}
		if (_layout.OutputMask) {
			Log::Trace("Vulkan::Shader", "    Attribute Outputs: {}", MaskToBindings(_layout.OutputMask));
		}
		if (_layout.SpecConstantMask) {
			Log::Trace("Vulkan::Shader", "    Specialization Constants: {}", MaskToBindings(_layout.SpecConstantMask));
		}
		if (_layout.PushConstantSize) {
			Log::Trace("Vulkan::Shader", "    Push Constant Size: {}B", _layout.PushConstantSize);
		}
	}
#endif
}

Shader::~Shader() noexcept {
	if (_shaderModule) { _device.GetDevice().destroyShaderModule(_shaderModule); }
}

ProgramBuilder::ProgramBuilder() {
	std::fill(_shaders.begin(), _shaders.end(), nullptr);
}

ProgramBuilder& ProgramBuilder::AddStage(ShaderStage stage, Shader* shader) {
	_shaders[int(stage)] = shader;

	return *this;
}

Program::Program(Hash hash, Device& device, Shader* vertex, Shader* fragment)
		: HashedObject<Program>(hash), _device(device) {
	std::fill(_shaders.begin(), _shaders.end(), nullptr);
	_shaders[static_cast<int>(ShaderStage::Vertex)]   = vertex;
	_shaders[static_cast<int>(ShaderStage::Fragment)] = fragment;

	Bake();
}

Program::Program(Hash hash, Device& device, Shader* compute) : HashedObject<Program>(hash), _device(device) {
	std::fill(_shaders.begin(), _shaders.end(), nullptr);
	_shaders[static_cast<int>(ShaderStage::Compute)] = compute;

	Bake();
}

Program::Program(Hash hash, Device& device, ProgramBuilder& builder) : HashedObject<Program>(hash), _device(device) {
	std::fill(_shaders.begin(), _shaders.end(), nullptr);
	for (int stage = 0; stage < ShaderStageCount; ++stage) { _shaders[stage] = builder._shaders[stage]; }

	Bake();
}

Program::~Program() noexcept {
#ifdef LUNA_VULKAN_MT
	for (auto& pipeline : _pipelines.GetReadOnly()) { _device.GetDevice().destroyPipeline(pipeline.Value); }
	for (auto& pipeline : _pipelines.GetReadWrite()) { _device.GetDevice().destroyPipeline(pipeline.Value); }
#else
	for (auto& pipeline : _pipelines) { _device.GetDevice().destroyPipeline(pipeline.Value); }
#endif
}

void Program::Bake() {
	if (_shaders[static_cast<int>(ShaderStage::Vertex)]) {
		_layout.AttributeMask = _shaders[static_cast<int>(ShaderStage::Vertex)]->GetResourceLayout().InputMask;
	}
	if (_shaders[static_cast<int>(ShaderStage::Fragment)]) {
		_layout.RenderTargetMask = _shaders[static_cast<int>(ShaderStage::Fragment)]->GetResourceLayout().OutputMask;
	}

	for (int i = 0; i < ShaderStageCount; ++i) {
		const auto* shader = _shaders[i];
		if (!shader) { continue; }

		const auto& shaderLayout = shader->GetResourceLayout();
		uint32_t stageMask       = 1u << i;

		if (shaderLayout.PushConstantSize) {
			_layout.PushConstantRange.stageFlags |= static_cast<vk::ShaderStageFlagBits>(stageMask);
			_layout.PushConstantRange.size = std::max(_layout.PushConstantRange.size, shaderLayout.PushConstantSize);
		}

		_layout.SpecConstantMask[i] = shaderLayout.SpecConstantMask;
		_layout.CombinedSpecConstantMask |= shaderLayout.SpecConstantMask;
		_layout.BindlessDescriptorSetMask |= shaderLayout.BindlessSetMask;

		for (uint32_t set = 0; set < MaxDescriptorSets; ++set) {
			_layout.SetLayouts[set].FloatMask |= shaderLayout.SetLayouts[set].FloatMask;
			_layout.SetLayouts[set].ImmutableSamplerMask |= shaderLayout.SetLayouts[set].ImmutableSamplerMask;
			_layout.SetLayouts[set].InputAttachmentMask |= shaderLayout.SetLayouts[set].InputAttachmentMask;
			_layout.SetLayouts[set].SampledBufferMask |= shaderLayout.SetLayouts[set].SampledBufferMask;
			_layout.SetLayouts[set].SampledImageMask |= shaderLayout.SetLayouts[set].SampledImageMask;
			_layout.SetLayouts[set].SamplerMask |= shaderLayout.SetLayouts[set].SamplerMask;
			_layout.SetLayouts[set].SeparateImageMask |= shaderLayout.SetLayouts[set].SeparateImageMask;
			_layout.SetLayouts[set].StorageBufferMask |= shaderLayout.SetLayouts[set].StorageBufferMask;
			_layout.SetLayouts[set].StorageImageMask |= shaderLayout.SetLayouts[set].StorageImageMask;
			_layout.SetLayouts[set].UniformBufferMask |= shaderLayout.SetLayouts[set].UniformBufferMask;

			const uint32_t activeBinds =
				shaderLayout.SetLayouts[set].InputAttachmentMask | shaderLayout.SetLayouts[set].SampledBufferMask |
				shaderLayout.SetLayouts[set].SampledImageMask | shaderLayout.SetLayouts[set].SamplerMask |
				shaderLayout.SetLayouts[set].SeparateImageMask | shaderLayout.SetLayouts[set].StorageBufferMask |
				shaderLayout.SetLayouts[set].StorageImageMask | shaderLayout.SetLayouts[set].UniformBufferMask;
			if (activeBinds) { _layout.StagesForSets[set] |= stageMask; }

			ForEachBit(activeBinds, [&](uint32_t bit) {
				_layout.StagesForBindings[set][bit] |= stageMask;
				auto& combinedSize     = _layout.SetLayouts[set].ArraySizes[bit];
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
		if (_layout.StagesForSets[set]) {
			_layout.DescriptorSetMask |= 1u << set;
			for (uint32_t binding = 0; binding < MaxDescriptorBindings; ++binding) {
				auto& arraySize = _layout.SetLayouts[set].ArraySizes[binding];
				if (arraySize == DescriptorSetLayout::UnsizedArray) {
					for (uint32_t i = 1; i < MaxDescriptorBindings; ++i) {
						if (_layout.StagesForBindings[set][i]) {
							Log::Error("Vulkan::Program",
							           "Reflection error: Set {}, binding {} is bindless, but binding {} has descriptors.",
							           set,
							           binding,
							           i);
						}
					}
					_layout.StagesForBindings[set][binding] = static_cast<uint32_t>(vk::ShaderStageFlagBits::eAll);
				} else if (arraySize == 0) {
					arraySize = 1;
				}
			}
		}
	}

	Hasher h;
	h(_layout.PushConstantRange.stageFlags);
	h(_layout.PushConstantRange.size);
	_layout.PushConstantLayoutHash = h.Get();

	_pipelineLayout = _device.RequestPipelineLayout(_layout);

// Dump program resources to console.
#if 1
	{
		Log::Trace("Vulkan::Program", "- Program Resources:");

		for (int i = 0; i < MaxDescriptorSets; ++i) {
			const auto& set = _layout.SetLayouts[i];

			if ((set.FloatMask | set.ImmutableSamplerMask | set.InputAttachmentMask | set.SampledBufferMask |
			     set.SampledImageMask | set.SamplerMask | set.SeparateImageMask | set.StorageBufferMask |
			     set.StorageImageMask | set.UniformBufferMask) == 0) {
				continue;
			}

			Log::Trace("Vulkan::Program", "    Descriptor Set {}:", i);
			Log::Trace("Vulkan::Program",
			           "      Stages: {}",
			           vk::to_string(static_cast<vk::ShaderStageFlags>(_layout.StagesForSets[i])));
			if (set.FloatMask) {
				Log::Trace("Vulkan::Program", "      Floating Point Images: {}", MaskToBindings(set.FloatMask, set.ArraySizes));
			}
			if (set.ImmutableSamplerMask) {
				Log::Trace(
					"Vulkan::Program", "      Immutable Samplers: {}", MaskToBindings(set.ImmutableSamplerMask, set.ArraySizes));
			}
			if (set.InputAttachmentMask) {
				Log::Trace(
					"Vulkan::Program", "      Input Attachments: {}", MaskToBindings(set.InputAttachmentMask, set.ArraySizes));
			}
			if (set.SampledBufferMask) {
				Log::Trace(
					"Vulkan::Program", "      Sampled Buffers: {}", MaskToBindings(set.SampledBufferMask, set.ArraySizes));
			}
			if (set.SampledImageMask) {
				Log::Trace("Vulkan::Program", "      Sampled Images: {}", MaskToBindings(set.SampledImageMask, set.ArraySizes));
			}
			if (set.SamplerMask) {
				Log::Trace("Vulkan::Program", "      Samplers: {}", MaskToBindings(set.SamplerMask, set.ArraySizes));
			}
			if (set.SeparateImageMask) {
				Log::Trace(
					"Vulkan::Program", "      Separate Images: {}", MaskToBindings(set.SeparateImageMask, set.ArraySizes));
			}
			if (set.StorageBufferMask) {
				Log::Trace(
					"Vulkan::Program", "      Storage Buffers: {}", MaskToBindings(set.StorageBufferMask, set.ArraySizes));
			}
			if (set.StorageImageMask) {
				Log::Trace("Vulkan::Program", "      Storage Images: {}", MaskToBindings(set.StorageImageMask, set.ArraySizes));
			}
			if (set.UniformBufferMask) {
				Log::Trace(
					"Vulkan::Program", "      Uniform Buffers: {}", MaskToBindings(set.UniformBufferMask, set.ArraySizes));
			}
		}

		if (_layout.AttributeMask) {
			Log::Trace("Vulkan::Program", "    Input Attributes: {}", MaskToBindings(_layout.AttributeMask));
		}
		if (_layout.BindlessDescriptorSetMask) {
			Log::Trace("Vulkan::Program", "    Bindless Sets: {}", MaskToBindings(_layout.BindlessDescriptorSetMask));
		}
		if (_layout.CombinedSpecConstantMask) {
			Log::Trace(
				"Vulkan::Program", "    Specialization Constants: {}", MaskToBindings(_layout.CombinedSpecConstantMask));
			for (uint32_t stage = 0; stage < ShaderStageCount; ++stage) {
				if (_layout.SpecConstantMask[stage]) {
					Log::Trace("Vulkan::Program",
					           "      {}: {}",
					           vk::to_string(static_cast<vk::ShaderStageFlagBits>(stage)),
					           MaskToBindings(_layout.SpecConstantMask[stage]));
				}
			}
		}
		if (_layout.DescriptorSetMask) {
			Log::Trace("Vulkan::Program", "    Descriptor Sets: {}", MaskToBindings(_layout.DescriptorSetMask));
		}
		if (_layout.RenderTargetMask) {
			Log::Trace("Vulkan::Program", "    Render Targets: {}", MaskToBindings(_layout.RenderTargetMask));
		}
		if (_layout.PushConstantRange.size) {
			Log::Trace("Vulkan::Program",
			           "    Push Constant: {}B in {}",
			           _layout.PushConstantRange.size,
			           vk::to_string(_layout.PushConstantRange.stageFlags));
		}
	}
#endif
}

vk::Pipeline Program::AddPipeline(Hash hash, vk::Pipeline pipeline) const {
	return _pipelines.EmplaceYield(hash, pipeline)->Value;
}

vk::Pipeline Program::GetPipeline(Hash hash) const {
	auto* ret = _pipelines.Find(hash);
	return ret ? ret->Value : VK_NULL_HANDLE;
}
}  // namespace Vulkan
}  // namespace Luna
