#include <Luna/Threading/Threading.hpp>
#include <Luna/Vulkan/DescriptorSet.hpp>
#include <Luna/Vulkan/Device.hpp>

namespace Luna {
namespace Vulkan {
DescriptorSetAllocator::DescriptorSetNode::DescriptorSetNode(vk::DescriptorSet set) : Set(set) {}

DescriptorSetAllocator::DescriptorSetAllocator(Hash hash,
                                               Device& device,
                                               const DescriptorSetLayout& layout,
                                               const uint32_t* stagesForBindings)
		: HashedObject<DescriptorSetAllocator>(hash), _device(device) {
	_bindless = layout.ArraySizes[0] == DescriptorSetLayout::UnsizedArray;

	if (!_bindless) {
		const uint32_t threadCount = Threading::Get()->GetThreadCount();
		for (uint32_t i = 0; i < threadCount; ++i) { _perThread.emplace_back(); }
	}

	std::vector<vk::DescriptorSetLayoutBinding> bindings;

	for (uint32_t binding = 0; binding < MaxDescriptorBindings; ++binding) {
		const auto stages = static_cast<vk::ShaderStageFlags>(stagesForBindings[binding]);
		if (!stages) { continue; }

		const uint32_t arraySize     = layout.ArraySizes[binding];
		const uint32_t poolArraySize = arraySize * DescriptorSetsPerPool;

		uint32_t types = 0;

		if (layout.SampledImageMask & (1u << binding)) {
			bindings.push_back({binding, vk::DescriptorType::eCombinedImageSampler, arraySize, stages, nullptr});
			_poolSizes.push_back({vk::DescriptorType::eCombinedImageSampler, poolArraySize});
			++types;
		}
		if (layout.SampledBufferMask & (1u << binding)) {
			bindings.push_back({binding, vk::DescriptorType::eUniformTexelBuffer, arraySize, stages, nullptr});
			_poolSizes.push_back({vk::DescriptorType::eUniformTexelBuffer, poolArraySize});
			++types;
		}
		if (layout.StorageImageMask & (1u << binding)) {
			bindings.push_back({binding, vk::DescriptorType::eStorageImage, arraySize, stages, nullptr});
			_poolSizes.push_back({vk::DescriptorType::eStorageImage, poolArraySize});
			++types;
		}
		if (layout.UniformBufferMask & (1u << binding)) {
			bindings.push_back({binding, vk::DescriptorType::eUniformBuffer, arraySize, stages, nullptr});
			_poolSizes.push_back({vk::DescriptorType::eUniformBuffer, poolArraySize});
			++types;
		}
		if (layout.StorageBufferMask & (1u << binding)) {
			bindings.push_back({binding, vk::DescriptorType::eStorageBuffer, arraySize, stages, nullptr});
			_poolSizes.push_back({vk::DescriptorType::eStorageBuffer, poolArraySize});
			++types;
		}
		if (layout.InputAttachmentMask & (1u << binding)) {
			bindings.push_back({binding, vk::DescriptorType::eInputAttachment, arraySize, stages, nullptr});
			_poolSizes.push_back({vk::DescriptorType::eInputAttachment, poolArraySize});
			++types;
		}
		if (layout.SeparateImageMask & (1u << binding)) {
			bindings.push_back({binding, vk::DescriptorType::eSampledImage, arraySize, stages, nullptr});
			_poolSizes.push_back({vk::DescriptorType::eSampledImage, poolArraySize});
			++types;
		}
		if (layout.SamplerMask & (1u << binding)) {
			bindings.push_back({binding, vk::DescriptorType::eSampler, arraySize, stages, nullptr});
			_poolSizes.push_back({vk::DescriptorType::eSampler, poolArraySize});
			++types;
		}
	}

	const vk::DescriptorSetLayoutCreateInfo layoutCI({}, bindings);
	_setLayout = _device.GetDevice().createDescriptorSetLayout(layoutCI);
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

	std::array<vk::DescriptorSetLayout, DescriptorSetsPerPool> layouts;
	std::fill(layouts.begin(), layouts.end(), _setLayout);
	const vk::DescriptorSetAllocateInfo setAI(pool, layouts);
	auto sets = _device.GetDevice().allocateDescriptorSets(setAI);
	state.Pools.push_back(pool);
	for (auto set : sets) { state.SetNodes.MakeVacant(set); }

	return {state.SetNodes.RequestVacant(hash)->Set, false};
}
}  // namespace Vulkan
}  // namespace Luna
