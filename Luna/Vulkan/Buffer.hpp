#pragma once

#include <vk_mem_alloc.h>

#include "Common.hpp"
#include "Cookie.hpp"
#include "InternalSync.hpp"

namespace Luna {
namespace Vulkan {
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
