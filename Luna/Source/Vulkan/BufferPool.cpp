#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/BufferPool.hpp>
#include <Luna/Vulkan/Device.hpp>

namespace Luna {
namespace Vulkan {
BufferBlock::~BufferBlock() noexcept {}

BufferBlockAllocation BufferBlock::Allocate(vk::DeviceSize allocateSize) {
	const auto alignedOffset = (Offset + Alignment - 1) & ~(Alignment - 1);
	if (alignedOffset + allocateSize <= Size) {
		auto* ret = Mapped + alignedOffset;
		Offset    = alignedOffset + allocateSize;

		// TODO: What is this "padded size" and why do we need to return such a large size when we're only using a portion
		// of the buffer?
		vk::DeviceSize paddedSize = std::max<vk::DeviceSize>(allocateSize, SpillSize);
		paddedSize                = std::min<vk::DeviceSize>(paddedSize, Size - alignedOffset);
		// vk::DeviceSize paddedSize = allocateSize;

		return {ret, alignedOffset, paddedSize};
	} else {
		return {nullptr, 0, 0};
	}
}

BufferPool::BufferPool(
	Device& device, vk::DeviceSize blockSize, vk::DeviceSize alignment, vk::BufferUsageFlags usage, bool deviceLocal)
		: _device(device), _blockSize(blockSize), _alignment(alignment), _usage(usage), _deviceLocal(deviceLocal) {}

BufferPool::~BufferPool() noexcept {}

BufferBlock BufferPool::RequestBlock(vk::DeviceSize minSize) {
	if ((minSize > _blockSize) || _blocks.empty()) { return AllocateBlock(std::max(_blockSize, minSize)); }

	auto back = std::move(_blocks.back());
	_blocks.pop_back();

	back.Mapped = static_cast<uint8_t*>(back.Cpu->Map());
	back.Offset = 0;

	return back;
}

void BufferPool::RecycleBlock(BufferBlock& block) {
	if (_blocks.size() < _maxRetainedBlocks) {
		_blocks.push_back(std::move(block));
	} else {
		block = {};
	}
}

void BufferPool::SetMaxRetainedBlocks(size_t maxBlocks) {
	_maxRetainedBlocks = maxBlocks;
}

void BufferPool::SetSpillRegionSize(vk::DeviceSize spillSize) {
	_spillSize = spillSize;
}

void BufferPool::Reset() {
	_blocks.clear();
}

BufferBlock BufferPool::AllocateBlock(vk::DeviceSize size) {
	BufferBlock block;

	const BufferDomain domain  = _deviceLocal ? BufferDomain::Device : BufferDomain::Host;
	vk::BufferUsageFlags usage = _usage;
	if (domain == BufferDomain::Device) { usage |= vk::BufferUsageFlagBits::eTransferDst; }

	BufferCreateInfo info(domain, size, usage);
	block.Gpu = _device.CreateBuffer(info);
	block.Gpu->SetInternalSync();

	block.Mapped = static_cast<uint8_t*>(block.Gpu->Map());
	if (!block.Mapped) {
		BufferCreateInfo cpuInfo(BufferDomain::Host, size, vk::BufferUsageFlagBits::eTransferSrc);
		block.Cpu = _device.CreateBuffer(cpuInfo);
		block.Cpu->SetInternalSync();
		block.Mapped = static_cast<uint8_t*>(block.Cpu->Map());
	} else {
		block.Cpu = block.Gpu;
	}

	block.Offset    = 0;
	block.Alignment = _alignment;
	block.Size      = size;
	block.SpillSize = _spillSize;

	return block;
}
}  // namespace Vulkan
}  // namespace Luna
