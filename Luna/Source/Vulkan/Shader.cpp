#include <Luna/Core/Log.hpp>
#include <Luna/Utility/BitOps.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Shader.hpp>
#include <spirv_cross.hpp>

namespace Luna {
namespace Vulkan {
Shader::Shader(Hash hash, Device& device, size_t codeSize, const void* code)
		: HashedObject<Shader>(hash), _device(device) {
	Log::Trace("[Vulkan::Shader] Creating new Shader.");

	assert(codeSize % 4 == 0);

	const vk::ShaderModuleCreateInfo shaderCI({}, codeSize, reinterpret_cast<const uint32_t*>(code));
	_shaderModule = _device.GetDevice().createShaderModule(shaderCI);

	// Reflect the SPIR-V code and find the shader's resources.
	{
		const auto UpdateArrayInfo = [&](const spirv_cross::SPIRType& type, uint32_t set, uint32_t binding) {
			auto& size = _layout.SetLayouts[set].ArraySizes[binding];
			if (!type.array.empty()) {
				if (type.array.size() != 1) {
					Log::Error("[Vulkan::Shader] Reflection error: Array dimension must be 1.");
				} else if (!type.array_size_literal.front()) {
					Log::Error("[Vulkan::Shader] Reflection error: Array dimension must be a litera.");
				} else {
					if (type.array.front() == 0) {
						if (binding != 0) {
							Log::Error("[Vulkan::Shader] Reflection error: Bindless textures can only be used with binding 0.");
						}

						if (type.basetype != spirv_cross::SPIRType::Image || type.image.dim == spv::DimBuffer) {
							Log::Error("[Vulkan::Shader] Reflection error: Bindless can only be used for sampled images.");
						} else {
							_layout.BindlessSetMask |= 1u << set;
						}

						size = DescriptorSetLayout::UnsizedArray;
					} else if (size && size != type.array.front()) {
						Log::Error("[Vulkan::Shader] Reflection error: Array dimension for set {}, binding {} is inconsistent.",
						           set,
						           binding);
					} else if (type.array.front() + binding > MaxDescriptorBindings) {
						Log::Error("[Vulkan::Shader] Reflection error: Array will go out of bounds.");
					} else {
						size = static_cast<uint8_t>(type.array.front());
					}
				}
			} else {
				if (size && size != 1) {
					Log::Error(
						"[Vulkan::Shader] Reflection error: Array dimension for set {}, binding {} is inconsistent.", set, binding);
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
			if (constant.constant_id >= MaxSpecializationConstants) {
				Log::Error("[Vulkan::Shader] Reflection error: Specialization constant {} is out of range and will be ignored. "
				           "Max allowed is {}.",
				           constant.constant_id,
				           MaxSpecializationConstants);
				continue;
			}

			_layout.SpecConstantMask |= 1u << constant.constant_id;
		}
	}

	// Dump shader resources to console.
	{
		const auto MaskToBindings = [](uint32_t mask, const uint8_t* arraySizes = nullptr) -> std::string {
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
		};

		Log::Trace("[Vulkan::Shader] - Shader Resources:");

		for (int i = 0; i < MaxDescriptorSets; ++i) {
			const auto& set = _layout.SetLayouts[i];

			if ((set.FloatMask | set.ImmutableSamplerMask | set.InputAttachmentMask | set.SampledBufferMask |
			     set.SampledImageMask | set.SamplerMask | set.SeparateImageMask | set.StorageBufferMask |
			     set.StorageImageMask | set.UniformBufferMask) == 0) {
				continue;
			}

			Log::Trace("[Vulkan::Shader]     Descriptor Set {}:", i);
			if (set.FloatMask) {
				Log::Trace("[Vulkan::Shader]       Floating Point Images: {}", MaskToBindings(set.FloatMask, set.ArraySizes));
			}
			if (set.ImmutableSamplerMask) {
				Log::Trace("[Vulkan::Shader]       Immutable Samplers: {}",
				           MaskToBindings(set.ImmutableSamplerMask, set.ArraySizes));
			}
			if (set.InputAttachmentMask) {
				Log::Trace("[Vulkan::Shader]       Input Attachments: {}",
				           MaskToBindings(set.InputAttachmentMask, set.ArraySizes));
			}
			if (set.SampledBufferMask) {
				Log::Trace("[Vulkan::Shader]       Sampled Buffers: {}", MaskToBindings(set.SampledBufferMask, set.ArraySizes));
			}
			if (set.SampledImageMask) {
				Log::Trace("[Vulkan::Shader]       Sampled Images: {}", MaskToBindings(set.SampledImageMask, set.ArraySizes));
			}
			if (set.SamplerMask) {
				Log::Trace("[Vulkan::Shader]       Samplers: {}", MaskToBindings(set.SamplerMask, set.ArraySizes));
			}
			if (set.SeparateImageMask) {
				Log::Trace("[Vulkan::Shader]       Separate Images: {}", MaskToBindings(set.SeparateImageMask, set.ArraySizes));
			}
			if (set.StorageBufferMask) {
				Log::Trace("[Vulkan::Shader]       Storage Buffers: {}", MaskToBindings(set.StorageBufferMask, set.ArraySizes));
			}
			if (set.StorageImageMask) {
				Log::Trace("[Vulkan::Shader]       Storage Images: {}", MaskToBindings(set.StorageImageMask, set.ArraySizes));
			}
			if (set.UniformBufferMask) {
				Log::Trace("[Vulkan::Shader]       Uniform Buffers: {}", MaskToBindings(set.UniformBufferMask, set.ArraySizes));
			}
		}

		if (_layout.BindlessSetMask) {
			Log::Trace("[Vulkan::Shader]     Bindless Sets: {}", MaskToBindings(_layout.BindlessSetMask));
		}
		if (_layout.InputMask) {
			Log::Trace("[Vulkan::Shader]     Attribute Inputs: {}", MaskToBindings(_layout.InputMask));
		}
		if (_layout.OutputMask) {
			Log::Trace("[Vulkan::Shader]     Attribute Outputs: {}", MaskToBindings(_layout.OutputMask));
		}
		if (_layout.SpecConstantMask) {
			Log::Trace("[Vulkan::Shader]     Specialization Constants: {}", MaskToBindings(_layout.SpecConstantMask));
		}
		if (_layout.PushConstantSize) {
			Log::Trace("[Vulkan::Shader]     Push Constant Size: {}B", _layout.PushConstantSize);
		}
	}
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
