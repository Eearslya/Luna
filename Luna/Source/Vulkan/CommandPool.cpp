#include <Luna/Core/Log.hpp>
#include <Luna/Vulkan/CommandPool.hpp>
#include <Luna/Vulkan/Device.hpp>

namespace Luna {
namespace Vulkan {
CommandPool::CommandPool(Device& device, uint32_t familyIndex) : _device(device) {
	Log::Trace("[Vulkan::CommandPool] Creating new command pool on queue family {}.", familyIndex);

	const vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient, familyIndex);
	_pool = _device._device.createCommandPool(poolCI);
}

CommandPool::~CommandPool() noexcept {
	if (_pool) { _device._device.destroyCommandPool(_pool); }
}

vk::CommandBuffer CommandPool::RequestCommandBuffer() {
	assert(_bufferIndex <= _commandBuffers.size());

	if (_bufferIndex == _commandBuffers.size()) {
		const vk::CommandBufferAllocateInfo bufferAI(_pool, vk::CommandBufferLevel::ePrimary, 1);
		auto buffers = _device._device.allocateCommandBuffers(bufferAI);
		_commandBuffers.push_back(buffers[0]);
	}

	return _commandBuffers[_bufferIndex++];
}

void CommandPool::Reset() {
	if (!_pool || _bufferIndex == 0) { return; }

	_bufferIndex = 0;
	_device._device.resetCommandPool(_pool);
}

void CommandPool::Trim() {
	if (!_pool) { return; }

	_device._device.resetCommandPool(_pool, vk::CommandPoolResetFlagBits::eReleaseResources);
	if (!_commandBuffers.empty()) { _device._device.freeCommandBuffers(_pool, _commandBuffers); }
	_bufferIndex = 0;
	_commandBuffers.clear();
	if (_device._extensions.Maintenance1) { _device._device.trimCommandPoolKHR(_pool, {}); }
}
}  // namespace Vulkan
}  // namespace Luna
