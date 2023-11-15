#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
struct DescriptorSetLayout {
	uint8_t ArraySizes[MaxDescriptorBindings] = {};
	uint32_t FloatMask                        = 0;
	uint32_t InputAttachmentMask              = 0;
	uint32_t SampledImageMask                 = 0;
	uint32_t SampledTexelBufferMask           = 0;
	uint32_t SamplerMask                      = 0;
	uint32_t SeparateImageMask                = 0;
	uint32_t StorageBufferMask                = 0;
	uint32_t StorageImageMask                 = 0;
	uint32_t StorageTexelBufferMask           = 0;
	uint32_t UniformBufferMask                = 0;

	constexpr static const uint8_t UnsizedArray = 0xff;
};

struct BindlessDescriptorPoolDeleter {
	void operator()(BindlessDescriptorPool* pool);
};

class BindlessDescriptorPool : public VulkanObject<BindlessDescriptorPool, BindlessDescriptorPoolDeleter>,
															 public InternalSyncEnabled {
	friend struct BindlessDescriptorPoolDeleter;

 public:
 private:
};

class BindlessAllocator {
 public:
 private:
};

class DescriptorSetAllocator : public HashedObject<DescriptorSetAllocator> {
	constexpr static const int DescriptorSetRingSize = 8;

 public:
	DescriptorSetAllocator(Hash hash,
	                       Device& device,
	                       const DescriptorSetLayout& layout,
	                       const vk::ShaderStageFlags* stagesForBindings);
	~DescriptorSetAllocator() noexcept;

	[[nodiscard]] vk::DescriptorSetLayout GetSetLayout() const {
		return _setLayout;
	}
	[[nodiscard]] bool IsBindless() const {
		return _bindless;
	}

	void BeginFrame();
	void Clear();
	std::pair<vk::DescriptorSet, bool> Find(uint32_t threadIndex, Hash hash);

 private:
	struct DescriptorSetNode : TemporaryHashMapEnabled<DescriptorSetNode>, IntrusiveListEnabled<DescriptorSetNode> {
		explicit DescriptorSetNode(vk::DescriptorSet set);
		vk::DescriptorSet Set;
	};

	struct PerThread {
		std::vector<vk::DescriptorPool> Pools;
		TemporaryHashMap<DescriptorSetNode, DescriptorSetRingSize, true> SetNodes;
		bool ShouldBegin = true;
	};

	Device& _device;
	vk::DescriptorSetLayout _setLayout;
	std::vector<std::unique_ptr<PerThread>> _perThread;
	std::vector<vk::DescriptorPoolSize> _poolSizes;
	bool _bindless = false;
};
}  // namespace Vulkan
}  // namespace Luna
