#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
static inline vk::AccessFlags BufferUsageToAccess(vk::BufferUsageFlags usage) {
	vk::AccessFlags flags;

	if (usage & vk::BufferUsageFlagBits::eTransferDst) { flags |= vk::AccessFlagBits::eTransferWrite; }
	if (usage & vk::BufferUsageFlagBits::eTransferSrc) { flags |= vk::AccessFlagBits::eTransferRead; }
	if (usage & vk::BufferUsageFlagBits::eVertexBuffer) { flags |= vk::AccessFlagBits::eVertexAttributeRead; }
	if (usage & vk::BufferUsageFlagBits::eIndexBuffer) { flags |= vk::AccessFlagBits::eIndexRead; }
	if (usage & vk::BufferUsageFlagBits::eIndirectBuffer) { flags |= vk::AccessFlagBits::eIndirectCommandRead; }
	if (usage & vk::BufferUsageFlagBits::eUniformBuffer) { flags |= vk::AccessFlagBits::eUniformRead; }
	if (usage & vk::BufferUsageFlagBits::eStorageBuffer) {
		flags |= vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
	}

	return flags;
}

static inline vk::PipelineStageFlags BufferUsageToStages(vk::BufferUsageFlags usage) {
	vk::PipelineStageFlags flags;

	if (usage & (vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc)) {
		flags |= vk::PipelineStageFlagBits::eTransfer;
	}
	if (usage & (vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eVertexBuffer)) {
		flags |= vk::PipelineStageFlagBits::eVertexInput;
	}
	if (usage & vk::BufferUsageFlagBits::eIndirectBuffer) { flags |= vk::PipelineStageFlagBits::eDrawIndirect; }
	if (usage & (vk::BufferUsageFlagBits::eStorageTexelBuffer | vk::BufferUsageFlagBits::eUniformBuffer |
	             vk::BufferUsageFlagBits::eUniformTexelBuffer)) {
		flags |= vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eFragmentShader |
		         vk::PipelineStageFlagBits::eVertexShader;
	}
	if (usage & vk::BufferUsageFlagBits::eStorageBuffer) { flags |= vk::PipelineStageFlagBits::eComputeShader; }

	return flags;
}

enum class BufferDomain { Device, Host };

struct BufferCreateInfo {
	BufferCreateInfo() {}
	BufferCreateInfo(BufferDomain domain, vk::DeviceSize size, vk::BufferUsageFlags usage)
			: Domain(domain), Size(size), Usage(usage) {}

	BufferDomain Domain = BufferDomain::Device;
	vk::DeviceSize Size = 0;
	vk::BufferUsageFlags Usage;
};

struct BufferDeleter {
	void operator()(Buffer* buffer);
};

class Buffer : public IntrusivePtrEnabled<Buffer, BufferDeleter, HandleCounter>,
							 public Cookie,
							 public InternalSyncEnabled {
	friend struct BufferDeleter;
	friend class ObjectPool<Buffer>;

 public:
	~Buffer() noexcept;

	bool CanMap() const {
		return _mapped != nullptr;
	}
	VmaAllocation& GetAllocation() {
		return _allocation;
	}
	const VmaAllocation& GetAllocation() const {
		return _allocation;
	}
	vk::Buffer GetBuffer() const {
		return _buffer;
	}
	const BufferCreateInfo& GetCreateInfo() const {
		return _createInfo;
	}
	vk::MemoryPropertyFlags GetMemoryProperties() const {
		return _memoryProperties;
	}

	void* Map();
	void Unmap();

 private:
	Buffer(Device& device, const BufferCreateInfo& createInfo);

	Device& _device;
	vk::Buffer _buffer;
	VmaAllocation _allocation;
	BufferCreateInfo _createInfo;
	void* _mapped = nullptr;
	vk::MemoryPropertyFlags _memoryProperties;
};
}  // namespace Vulkan
}  // namespace Luna
