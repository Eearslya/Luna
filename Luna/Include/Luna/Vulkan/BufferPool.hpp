#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
struct BufferBlockAllocation {
	uint8_t* Host             = nullptr;
	vk::DeviceSize Offset     = 0;
	vk::DeviceSize PaddedSize = 0;
};

struct BufferBlock {
	~BufferBlock() noexcept;

	BufferBlockAllocation Allocate(vk::DeviceSize allocateSize);

	BufferHandle Cpu;
	BufferHandle Gpu;
	vk::DeviceSize Offset    = 0;
	vk::DeviceSize Alignment = 0;
	vk::DeviceSize Size      = 0;
	vk::DeviceSize SpillSize = 0;
	uint8_t* Mapped          = nullptr;
};

class BufferPool {
 public:
	BufferPool(
		Device& device, vk::DeviceSize blockSize, vk::DeviceSize alignment, vk::BufferUsageFlags usage, bool deviceLocal);
	BufferPool(const BufferPool&)     = delete;
	BufferPool(BufferPool&&)          = delete;
	void operator=(const BufferPool&) = delete;
	void operator=(BufferPool&&)      = delete;
	~BufferPool() noexcept;

	vk::DeviceSize GetBlockSize() const {
		return _blockSize;
	}

	BufferBlock RequestBlock(vk::DeviceSize minSize);
	void RecycleBlock(BufferBlock& block);
	void SetMaxRetainedBlocks(size_t maxBlocks);
	void SetSpillRegionSize(vk::DeviceSize spillSize);
	void Reset();

 private:
	BufferBlock AllocateBlock(vk::DeviceSize size);

	Device& _device;
	vk::DeviceSize _blockSize   = 0;
	vk::DeviceSize _alignment   = 0;
	vk::DeviceSize _spillSize   = 0;
	vk::BufferUsageFlags _usage = {};
	size_t _maxRetainedBlocks   = 0;
	std::vector<BufferBlock> _blocks;
	bool _deviceLocal = false;
};
}  // namespace Vulkan
}  // namespace Luna
