#include <Luna/Core/Threading.hpp>
#include <Luna/Vulkan/DescriptorSet.hpp>
#include <Luna/Vulkan/Device.hpp>

namespace Luna {
namespace Vulkan {
DescriptorSetAllocator::DescriptorSetNode::DescriptorSetNode(vk::DescriptorSet set) : Set(set) {}

DescriptorSetAllocator::DescriptorSetAllocator(Hash hash,
                                               Device& device,
                                               const DescriptorSetLayout& layout,
                                               const vk::ShaderStageFlags* stagesForBindings)
		: HashedObject<DescriptorSetAllocator>(hash), _device(device) {
	_bindless = layout.ArraySizes[0] == DescriptorSetLayout::UnsizedArray;

	const vk::DescriptorBindingFlags bindingFlags = vk::DescriptorBindingFlagBits::ePartiallyBound |
	                                                vk::DescriptorBindingFlagBits::eUpdateAfterBind |
	                                                vk::DescriptorBindingFlagBits::eVariableDescriptorCount;
	const vk::DescriptorSetLayoutBindingFlagsCreateInfo setBindingFlags(bindingFlags);
	vk::DescriptorSetLayoutCreateInfo layoutCI;

	if (!_bindless) {
		const uint32_t threadCount = Threading::GetThreadCount() + 1;
		for (uint32_t i = 0; i < threadCount; ++i) { _perThread.emplace_back(new PerThread()); }
	}

	std::vector<vk::DescriptorSetLayoutBinding> bindings;

	if (_bindless) {
		layoutCI.flags |= vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
		layoutCI.pNext = &setBindingFlags;
	}

	for (uint32_t binding = 0; binding < MaxDescriptorBindings; ++binding) {
		const auto stages = stagesForBindings[binding];
		if (!stages) { continue; }

		const uint32_t bindingMask   = 1u << binding;
		const uint32_t arraySize     = _bindless ? MaxBindlessDescriptors : layout.ArraySizes[binding];
		const uint32_t poolArraySize = _bindless ? arraySize : arraySize * DescriptorSetsPerPool;

		if (layout.InputAttachmentMask & bindingMask) {
			bindings.push_back({binding, vk::DescriptorType::eInputAttachment, arraySize, stages, nullptr});
			_poolSizes.push_back({vk::DescriptorType::eInputAttachment, poolArraySize});
		} else if (layout.SampledImageMask & bindingMask) {
			bindings.push_back({binding, vk::DescriptorType::eCombinedImageSampler, arraySize, stages, nullptr});
			_poolSizes.push_back({vk::DescriptorType::eCombinedImageSampler, poolArraySize});
		} else if (layout.SampledTexelBufferMask & bindingMask) {
			bindings.push_back({binding, vk::DescriptorType::eUniformTexelBuffer, arraySize, stages, nullptr});
			_poolSizes.push_back({vk::DescriptorType::eUniformTexelBuffer, poolArraySize});
		} else if (layout.SamplerMask & bindingMask) {
			bindings.push_back({binding, vk::DescriptorType::eSampler, arraySize, stages, nullptr});
			_poolSizes.push_back({vk::DescriptorType::eSampler, poolArraySize});
		} else if (layout.SeparateImageMask & bindingMask) {
			bindings.push_back({binding, vk::DescriptorType::eSampledImage, arraySize, stages, nullptr});
			_poolSizes.push_back({vk::DescriptorType::eSampledImage, poolArraySize});
		} else if (layout.StorageBufferMask & bindingMask) {
			bindings.push_back({binding, vk::DescriptorType::eStorageBuffer, arraySize, stages, nullptr});
			_poolSizes.push_back({vk::DescriptorType::eStorageBuffer, poolArraySize});
		} else if (layout.StorageImageMask & bindingMask) {
			bindings.push_back({binding, vk::DescriptorType::eStorageImage, arraySize, stages, nullptr});
			_poolSizes.push_back({vk::DescriptorType::eStorageImage, poolArraySize});
		} else if (layout.StorageTexelBufferMask & bindingMask) {
			bindings.push_back({binding, vk::DescriptorType::eStorageTexelBuffer, arraySize, stages, nullptr});
			_poolSizes.push_back({vk::DescriptorType::eStorageTexelBuffer, poolArraySize});
		} else if (layout.UniformBufferMask & bindingMask) {
			bindings.push_back({binding, vk::DescriptorType::eUniformBufferDynamic, arraySize, stages, nullptr});
			_poolSizes.push_back({vk::DescriptorType::eUniformBufferDynamic, poolArraySize});
		}
	}

	layoutCI.setBindings(bindings);
	_setLayout = _device.GetDevice().createDescriptorSetLayout(layoutCI);
	Log::Trace("Vulkan", "Descriptor Set Layout created.");
}

DescriptorSetAllocator::~DescriptorSetAllocator() noexcept {
	if (_setLayout) { _device.GetDevice().destroyDescriptorSetLayout(_setLayout); }
	Clear();
}

void DescriptorSetAllocator::BeginFrame() {
	if (!_bindless) {
		for (auto& t : _perThread) { t->ShouldBegin = true; }
	}
}

void DescriptorSetAllocator::Clear() {
	for (auto& t : _perThread) {
		t->SetNodes.Clear();
		for (auto& pool : t->Pools) {
			_device.GetDevice().resetDescriptorPool(pool);
			_device.GetDevice().destroyDescriptorPool(pool);
		}
		t->Pools.clear();
	}
}

std::pair<vk::DescriptorSet, bool> DescriptorSetAllocator::Find(uint32_t threadIndex, Hash hash) {
	auto& state = *_perThread[threadIndex];

	if (state.ShouldBegin) {
		state.SetNodes.BeginFrame();
		state.ShouldBegin = false;
	}

	auto* node = state.SetNodes.Request(hash);
	if (node) { return {node->Set, true}; }

	node = state.SetNodes.RequestVacant(hash);
	if (node) { return {node->Set, false}; }

	const vk::DescriptorPoolCreateInfo poolCI({}, DescriptorSetsPerPool, _poolSizes);
	auto pool = _device.GetDevice().createDescriptorPool(poolCI);
	state.Pools.push_back(pool);
	Log::Trace("Vulkan", "Descriptor Pool created.");

	std::array<vk::DescriptorSetLayout, DescriptorSetsPerPool> layouts;
	std::fill(layouts.begin(), layouts.end(), _setLayout);
	const vk::DescriptorSetAllocateInfo setAI(pool, layouts);
	auto sets = _device.GetDevice().allocateDescriptorSets(setAI);
	for (auto set : sets) { state.SetNodes.MakeVacant(set); }

	return {state.SetNodes.RequestVacant(hash)->Set, false};
}
}  // namespace Vulkan
}  // namespace Luna
