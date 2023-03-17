#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
vk::AccessFlags BufferUsageToAccess(vk::BufferUsageFlags usage);
vk::PipelineStageFlags BufferUsageToStages(vk::BufferUsageFlags usage);

enum class BufferDomain { Device, Host };

struct BufferCreateInfo {
	BufferDomain Domain        = BufferDomain::Device;
	vk::DeviceSize Size        = 0;
	vk::BufferUsageFlags Usage = {};
	BufferCreateFlags Flags    = {};
};

struct BufferDeleter {
	void operator()(Buffer* buffer);
};

class Buffer : public IntrusivePtrEnabled<Buffer, BufferDeleter, HandleCounter>,
							 public Cookie,
							 public InternalSyncEnabled {
	friend class ObjectPool<Buffer>;
	friend struct BufferDeleter;

 public:
	~Buffer() noexcept;

	const VmaAllocation& GetAllocation() const {
		return _allocation;
	}
	vk::Buffer GetBuffer() const {
		return _buffer;
	}
	const BufferCreateInfo& GetCreateInfo() const {
		return _createInfo;
	}
	vk::DeviceAddress GetDeviceAddress() const {
		return _deviceAddress;
	}
	void* Map() const {
		return _mappedMemory;
	}

 private:
	Buffer(Device& device,
	       vk::Buffer buffer,
	       const VmaAllocation& allocation,
	       const BufferCreateInfo& createInfo,
	       void* mappedMemory);

	Device& _device;
	vk::Buffer _buffer;
	VmaAllocation _allocation;
	BufferCreateInfo _createInfo;
	vk::DeviceAddress _deviceAddress = 0;
	void* _mappedMemory              = nullptr;
};
}  // namespace Vulkan
}  // namespace Luna
