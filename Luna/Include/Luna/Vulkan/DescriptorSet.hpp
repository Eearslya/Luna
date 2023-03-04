#pragma once

#include <Luna/Vulkan/Common.hpp>
#include <Luna/Vulkan/InternalSync.hpp>

namespace Luna {
namespace Vulkan {
struct DescriptorSetLayout {
	uint8_t ArraySizes[MaxDescriptorBindings] = {};
	uint32_t FloatMask                        = 0;
	uint32_t ImmutableSamplerMask             = 0;
	uint32_t InputAttachmentMask              = 0;
	uint32_t SampledBufferMask                = 0;
	uint32_t SampledImageMask                 = 0;
	uint32_t SamplerMask                      = 0;
	uint32_t SeparateImageMask                = 0;
	uint32_t StorageBufferMask                = 0;
	uint32_t StorageImageMask                 = 0;
	uint32_t UniformBufferMask                = 0;

	constexpr static const uint8_t UnsizedArray = 0xff;
};

struct BindlessDescriptorPoolDeleter {
	void operator()(BindlessDescriptorPool* pool);
};

class BindlessDescriptorPool
		: public IntrusivePtrEnabled<BindlessDescriptorPool, BindlessDescriptorPoolDeleter, HandleCounter>,
			public InternalSyncEnabled {
	friend struct BindlessDescriptorPoolDeleter;

 public:
	explicit BindlessDescriptorPool(Device& device,
	                                DescriptorSetAllocator* allocator,
	                                vk::DescriptorPool pool,
	                                uint32_t totalSets,
	                                uint32_t totalDescriptors);
	~BindlessDescriptorPool() noexcept;

	vk::DescriptorSet GetDescriptorSet() const {
		return _set;
	}

	bool AllocateDescriptors(uint32_t count);
	void Reset();
	void SetTexture(uint32_t binding, const ImageView& view);
	void SetTextureUnorm(uint32_t binding, const ImageView& view);
	void SetTextureSrgb(uint32_t binding, const ImageView& view);

 private:
	void SetTexture(uint32_t binding, vk::ImageView view, vk::ImageLayout layout);

	Device& _device;
	DescriptorSetAllocator* _allocator;
	vk::DescriptorPool _pool;
	vk::DescriptorSet _set;

	uint32_t _allocatedSets        = 0;
	uint32_t _totalSets            = 0;
	uint32_t _allocatedDescriptors = 0;
	uint32_t _totalDescriptors     = 0;
};

class DescriptorSetAllocator : public HashedObject<DescriptorSetAllocator> {
	constexpr static const int DescriptorSetRingSize = 8;

 public:
	DescriptorSetAllocator(Hash hash,
	                       Device& device,
	                       const DescriptorSetLayout& layout,
	                       const uint32_t* stagesForBindings);
	~DescriptorSetAllocator() noexcept;

	vk::DescriptorSetLayout GetSetLayout() const {
		return _setLayout;
	}
	bool IsBindless() const {
		return _bindless;
	}

	void BeginFrame();
	void Clear();
	std::pair<vk::DescriptorSet, bool> Find(uint32_t threadIndex, Hash hash);

	vk::DescriptorPool AllocateBindlessPool(uint32_t setCount, uint32_t descriptorCount);
	vk::DescriptorSet AllocateBindlessSet(vk::DescriptorPool pool, uint32_t descriptorCount);
	void ResetBindlessPool(vk::DescriptorPool pool);

 private:
	struct DescriptorSetNode : TemporaryHashMapEnabled<DescriptorSetNode>, IntrusiveListEnabled<DescriptorSetNode> {
		explicit DescriptorSetNode(vk::DescriptorSet set);
		vk::DescriptorSet Set;
	};

	Device& _device;
	vk::DescriptorSetLayout _setLayout;

	struct PerThread {
		std::vector<vk::DescriptorPool> Pools;
		TemporaryHashMap<DescriptorSetNode, DescriptorSetRingSize, true> SetNodes;
		bool ShouldBegin = true;
	};
	std::vector<std::unique_ptr<PerThread>> _perThread;
	std::vector<vk::DescriptorPoolSize> _poolSizes;
	bool _bindless = false;
};
}  // namespace Vulkan
}  // namespace Luna
