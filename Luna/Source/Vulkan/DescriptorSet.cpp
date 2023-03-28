#include <Luna/Utility/Log.hpp>
#include <Luna/Utility/Threading.hpp>
#include <Luna/Vulkan/DescriptorSet.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/Sampler.hpp>

namespace Luna {
namespace Vulkan {
void BindlessDescriptorPoolDeleter::operator()(BindlessDescriptorPool* pool) {
	pool->_device._bindlessDescriptorPoolPool.Free(pool);
}

BindlessDescriptorPool::BindlessDescriptorPool(Device& device,
                                               DescriptorSetAllocator* allocator,
                                               vk::DescriptorPool pool,
                                               uint32_t totalSets,
                                               uint32_t totalDescriptors)
		: _device(device), _allocator(allocator), _pool(pool), _totalSets(totalSets), _totalDescriptors(totalDescriptors) {}

BindlessDescriptorPool::~BindlessDescriptorPool() noexcept {
	if (_pool) {
		if (_internalSync) {
			_device.DestroyDescriptorPoolNoLock(_pool);
		} else {
			_device.DestroyDescriptorPool(_pool);
		}
	}
}

bool BindlessDescriptorPool::AllocateDescriptors(uint32_t count) {
	if (_allocatedSets >= _totalSets || _allocatedDescriptors >= _totalDescriptors) { return false; }

	_allocatedSets++;
	_allocatedDescriptors += count;
	_set = _allocator->AllocateBindlessSet(_pool, count);

	return bool(_set);
}

void BindlessDescriptorPool::Reset() {
	if (_pool) { _allocator->ResetBindlessPool(_pool); }
	_set                  = nullptr;
	_allocatedSets        = 0;
	_allocatedDescriptors = 0;
}

void BindlessDescriptorPool::SetTexture(uint32_t binding, const ImageView& view) {
	SetTexture(binding, view.GetFloatView(), view.GetImage()->GetLayout(vk::ImageLayout::eShaderReadOnlyOptimal));
}

void BindlessDescriptorPool::SetTextureUnorm(uint32_t binding, const ImageView& view) {
	SetTexture(binding, view.GetUnormView(), view.GetImage()->GetLayout(vk::ImageLayout::eShaderReadOnlyOptimal));
}

void BindlessDescriptorPool::SetTextureSrgb(uint32_t binding, const ImageView& view) {
	SetTexture(binding, view.GetSrgbView(), view.GetImage()->GetLayout(vk::ImageLayout::eShaderReadOnlyOptimal));
}

void BindlessDescriptorPool::SetTexture(uint32_t binding, vk::ImageView view, vk::ImageLayout layout) {
	const vk::DescriptorImageInfo imageInfo(nullptr, view, layout);
	const vk::WriteDescriptorSet write(
		_set, 0, binding, 1, vk::DescriptorType::eSampledImage, &imageInfo, nullptr, nullptr);
	_device.GetDevice().updateDescriptorSets(write, nullptr);
}

BindlessAllocator::BindlessAllocator(Device& device) : _device(device) {
	_descriptorPool = _device.CreateBindlessDescriptorPool(1, 16384);
	_descriptorPool->AllocateDescriptors(16384);
	_textures.resize(16384);
}

void BindlessAllocator::BeginFrame() {
	_textureCount = 0;
}

vk::DescriptorSet BindlessAllocator::Commit() {
	vk::DescriptorSet set = _descriptorPool->GetDescriptorSet();
	const vk::WriteDescriptorSet write(
		set, 0, 0, _textureCount, vk::DescriptorType::eCombinedImageSampler, _textures.data(), nullptr, nullptr);
	_device.GetDevice().updateDescriptorSets(write, nullptr);

	return set;
}

void BindlessAllocator::Reset() {
	_descriptorPool.Reset();
}

uint32_t BindlessAllocator::Texture(const ImageView& view, const Sampler& sampler) {
	return SetTexture(
		view.GetView(), sampler.GetSampler(), view.GetImage()->GetLayout(vk::ImageLayout::eShaderReadOnlyOptimal));
}

uint32_t BindlessAllocator::Texture(const ImageView& view, StockSampler sampler) {
	return Texture(view, _device.GetStockSampler(sampler));
}

uint32_t BindlessAllocator::SrgbTexture(const ImageView& view, const Sampler& sampler) {
	return SetTexture(
		view.GetSrgbView(), sampler.GetSampler(), view.GetImage()->GetLayout(vk::ImageLayout::eShaderReadOnlyOptimal));
}

uint32_t BindlessAllocator::SrgbTexture(const ImageView& view, StockSampler sampler) {
	return SrgbTexture(view, _device.GetStockSampler(sampler));
}

uint32_t BindlessAllocator::UnormTexture(const ImageView& view, const Sampler& sampler) {
	return SetTexture(
		view.GetUnormView(), sampler.GetSampler(), view.GetImage()->GetLayout(vk::ImageLayout::eShaderReadOnlyOptimal));
}

uint32_t BindlessAllocator::UnormTexture(const ImageView& view, StockSampler sampler) {
	return SrgbTexture(view, _device.GetStockSampler(sampler));
}

uint32_t BindlessAllocator::SetTexture(vk::ImageView view, vk::Sampler sampler, vk::ImageLayout layout) {
	const uint32_t index = _textureCount++;
	_textures[index]     = vk::DescriptorImageInfo(sampler, view, layout);

	vk::DescriptorSet set = _descriptorPool->GetDescriptorSet();
	const vk::WriteDescriptorSet write(
		set, 0, index, 1, vk::DescriptorType::eCombinedImageSampler, &_textures[index], nullptr, nullptr);
	_device.GetDevice().updateDescriptorSets(write, nullptr);

	return index;
}

DescriptorSetAllocator::DescriptorSetNode::DescriptorSetNode(vk::DescriptorSet set) : Set(set) {}

DescriptorSetAllocator::DescriptorSetAllocator(Hash hash,
                                               Device& device,
                                               const DescriptorSetLayout& layout,
                                               const uint32_t* stagesForBindings)
		: HashedObject<DescriptorSetAllocator>(hash), _device(device) {
	_bindless = layout.ArraySizes[0] == DescriptorSetLayout::UnsizedArray;

	const vk::DescriptorBindingFlags bindingFlags = vk::DescriptorBindingFlagBits::ePartiallyBound |
	                                                vk::DescriptorBindingFlagBits::eUpdateAfterBind |
	                                                vk::DescriptorBindingFlagBits::eVariableDescriptorCount;
	const vk::DescriptorSetLayoutBindingFlagsCreateInfo setBindingFlags(bindingFlags);
	vk::DescriptorSetLayoutCreateInfo layoutCI;

	if (!_bindless) {
		const uint32_t threadCount = Threading::Get()->GetThreadCount() + 1;
		for (uint32_t i = 0; i < threadCount; ++i) { _perThread.emplace_back(new PerThread()); }
	}

	std::vector<vk::DescriptorSetLayoutBinding> bindings;

	if (_bindless) {
		layoutCI.flags |= vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
		layoutCI.setPNext(&setBindingFlags);
	}

	for (uint32_t binding = 0; binding < MaxDescriptorBindings; ++binding) {
		const auto stages = static_cast<vk::ShaderStageFlags>(stagesForBindings[binding]);
		if (!stages) { continue; }

		const uint32_t arraySize     = _bindless ? MaxBindlessDescriptors : layout.ArraySizes[binding];
		const uint32_t poolArraySize = _bindless ? arraySize : arraySize * DescriptorSetsPerPool;

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
			bindings.push_back({binding, vk::DescriptorType::eUniformBufferDynamic, arraySize, stages, nullptr});
			_poolSizes.push_back({vk::DescriptorType::eUniformBufferDynamic, poolArraySize});
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

	if (!bindings.empty()) {
		layoutCI.setBindings(bindings);
		_setLayout = _device.GetDevice().createDescriptorSetLayout(layoutCI);
		Log::Trace("Vulkan", "Descriptor Set Layout created.");
	}
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
	Log::Trace("Vulkan", "Descriptor Pool created.");

	std::array<vk::DescriptorSetLayout, DescriptorSetsPerPool> layouts;
	std::fill(layouts.begin(), layouts.end(), _setLayout);
	const vk::DescriptorSetAllocateInfo setAI(pool, layouts);
	auto sets = _device.GetDevice().allocateDescriptorSets(setAI);
	state.Pools.push_back(pool);
	for (auto set : sets) { state.SetNodes.MakeVacant(set); }

	return {state.SetNodes.RequestVacant(hash)->Set, false};
}

vk::DescriptorPool DescriptorSetAllocator::AllocateBindlessPool(uint32_t setCount, uint32_t descriptorCount) {
	if (!_bindless) { return nullptr; }

	vk::DescriptorPoolSize poolSize = _poolSizes[0];
	if (descriptorCount > poolSize.descriptorCount) { return nullptr; }
	poolSize.descriptorCount = descriptorCount;

	const vk::DescriptorPoolCreateInfo poolCI(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind, setCount, poolSize);

	return _device.GetDevice().createDescriptorPool(poolCI);
}

vk::DescriptorSet DescriptorSetAllocator::AllocateBindlessSet(vk::DescriptorPool pool, uint32_t descriptorCount) {
	if (!pool || !_bindless) { return nullptr; }

	const vk::DescriptorSetVariableDescriptorCountAllocateInfo setCAI(1, &descriptorCount);
	const vk::DescriptorSetAllocateInfo setAI(pool, _setLayout, &setCAI);
	auto sets = _device.GetDevice().allocateDescriptorSets(setAI);

	return sets.empty() ? nullptr : sets[0];
}

void DescriptorSetAllocator::ResetBindlessPool(vk::DescriptorPool pool) {
	_device.GetDevice().resetDescriptorPool(pool);
}
}  // namespace Vulkan
}  // namespace Luna
