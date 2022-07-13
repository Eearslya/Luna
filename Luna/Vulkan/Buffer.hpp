#pragma once

#include <vk_mem_alloc.h>

#include "Common.hpp"
#include "Cookie.hpp"
#include "InternalSync.hpp"

namespace Luna {
namespace Vulkan {
inline vk::AccessFlags BufferUsageToAccess(vk::BufferUsageFlags usage) {
	vk::AccessFlags access = {};

	if (usage & (vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc)) {
		access |= vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite;
	}
	if (usage & vk::BufferUsageFlagBits::eVertexBuffer) { access |= vk::AccessFlagBits::eVertexAttributeRead; }
	if (usage & vk::BufferUsageFlagBits::eIndexBuffer) { access |= vk::AccessFlagBits::eIndexRead; }
	if (usage & vk::BufferUsageFlagBits::eIndirectBuffer) { access |= vk::AccessFlagBits::eIndirectCommandRead; }
	if (usage & vk::BufferUsageFlagBits::eUniformBuffer) { access |= vk::AccessFlagBits::eUniformRead; }
	if (usage & vk::BufferUsageFlagBits::eStorageBuffer) {
		access |= vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
	}

	return access;
}

inline vk::PipelineStageFlags BufferUsageToStages(vk::BufferUsageFlags usage) {
	vk::PipelineStageFlags stages = {};

	if (usage & (vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc)) {
		stages |= vk::PipelineStageFlagBits::eTransfer;
	}
	if (usage & (vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer)) {
		stages |= vk::PipelineStageFlagBits::eVertexInput;
	}
	if (usage & vk::BufferUsageFlagBits::eIndirectBuffer) { stages |= vk::PipelineStageFlagBits::eDrawIndirect; }
	if (usage & (vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eUniformTexelBuffer |
	             vk::BufferUsageFlagBits::eStorageTexelBuffer)) {
		stages |= vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eVertexShader |
		          vk::PipelineStageFlagBits::eFragmentShader;
	}
	if (usage & vk::BufferUsageFlagBits::eStorageBuffer) { stages |= vk::PipelineStageFlagBits::eComputeShader; }

	return stages;
}

enum class BufferDomain { Device, Host };

struct BufferCreateInfo {
	BufferCreateInfo(BufferDomain domain, vk::DeviceSize size, vk::BufferUsageFlags usage)
			: Domain(domain), Size(size), Usage(usage) {}

	BufferDomain Domain        = BufferDomain::Device;
	vk::DeviceSize Size        = 0;
	vk::BufferUsageFlags Usage = {};
};

struct BufferDeleter {
	void operator()(Buffer* buffer);
};

class Buffer : public IntrusivePtrEnabled<Buffer, BufferDeleter, HandleCounter>, public Cookie, public InternalSync {
 public:
	friend struct BufferDeleter;
	friend class ObjectPool<Buffer>;

	~Buffer() noexcept;

	vk::Buffer GetBuffer() const {
		return _buffer;
	}
	const BufferCreateInfo& GetCreateInfo() const {
		return _createInfo;
	}

	void* Map() const {
		return _mappedMemory;
	}

 private:
	Buffer(Device& device,
	       vk::Buffer buffer,
	       const VmaAllocation& allocation,
	       const BufferCreateInfo& bufferCI,
	       void* mappedMemory);

	Device& _device;
	vk::Buffer _buffer;
	VmaAllocation _allocation;
	BufferCreateInfo _createInfo;
	void* _mappedMemory = nullptr;
};
}  // namespace Vulkan
}  // namespace Luna
