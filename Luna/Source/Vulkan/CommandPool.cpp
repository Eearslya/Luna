#include <Luna/Vulkan/CommandPool.hpp>
#include <Luna/Vulkan/Device.hpp>

namespace Luna {
namespace Vulkan {
CommandPool::CommandPool(Device& device, uint32_t familyIndex, const std::string& debugName)
		: _device(device), _debugName(debugName) {
	const auto vkDevice = _device._device;
	const vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient, familyIndex);
	_commandPool = vkDevice.createCommandPool(poolCI);
	Log::Trace("Vulkan", "Command Pool created.");
	_device.SetObjectName(_commandPool, debugName);
}

CommandPool::CommandPool(CommandPool&& other) noexcept : _device(other._device) {
	*this = std::move(other);
}

CommandPool& CommandPool::operator=(CommandPool&& other) noexcept {
	if (this != &other) {
		_commandPool        = other._commandPool;
		_debugName          = std::move(other._debugName);
		_commandBuffers     = std::move(other._commandBuffers);
		_commandBufferIndex = other._commandBufferIndex;

		other._commandPool = nullptr;
		other._debugName.clear();
		other._commandBuffers.clear();
		other._commandBufferIndex = 0;
	}

	return *this;
}

CommandPool::~CommandPool() noexcept {
	if (_commandPool) {
		if (!_commandBuffers.empty()) { _device._device.freeCommandBuffers(_commandPool, _commandBuffers); }
		_device._device.destroyCommandPool(_commandPool);
	}
}

void CommandPool::Begin() {
	if (_commandBufferIndex > 0) { _device._device.resetCommandPool(_commandPool); }
	_commandBufferIndex = 0;
}

vk::CommandBuffer CommandPool::RequestCommandBuffer() {
	if (_commandBufferIndex >= _commandBuffers.size()) {
		const vk::CommandBufferAllocateInfo bufferAI(_commandPool, vk::CommandBufferLevel::ePrimary, 1);
		const auto buffers = _device._device.allocateCommandBuffers(bufferAI);
		_commandBuffers.push_back(buffers[0]);
		++_commandBufferIndex;

		return buffers[0];
	}

	return _commandBuffers[_commandBufferIndex++];
}

void CommandPool::Trim() {
	_device._device.resetCommandPool(_commandPool);
	if (!_commandBuffers.empty()) { _device._device.freeCommandBuffers(_commandPool, _commandBuffers); }
	_commandBuffers.clear();
	_commandBufferIndex = 0;
	_device._device.trimCommandPool(_commandPool, {});
}
}  // namespace Vulkan
}  // namespace Luna
