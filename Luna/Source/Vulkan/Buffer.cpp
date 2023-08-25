#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>

namespace Luna {
namespace Vulkan {
void BufferDeleter::operator()(Buffer* buffer) {
	buffer->_device._bufferPool.Free(buffer);
}

Buffer::Buffer(Device& device,
               const BufferCreateInfo& createInfo,
               const void* initialData,
               const std::string& debugName)
		: Cookie(device), _device(device), _createInfo(createInfo), _debugName(debugName) {
	// First perform a few sanity checks.
	const bool zeroInitialize = createInfo.Flags & BufferCreateFlagBits::ZeroInitialize;

	// Convert our create info to Vulkan's create info
	const auto queueFamilies    = _device._queueInfo.UniqueFamilies();
	VkBufferCreateInfo bufferCI = {};
	bufferCI.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCI.size               = createInfo.Size;
	bufferCI.usage = static_cast<VkBufferCreateFlags>(createInfo.Usage | vk::BufferUsageFlagBits::eTransferDst |
	                                                  vk::BufferUsageFlagBits::eTransferSrc);
	if (queueFamilies.size() > 1) {
		bufferCI.sharingMode           = VK_SHARING_MODE_CONCURRENT;
		bufferCI.queueFamilyIndexCount = queueFamilies.size();
		bufferCI.pQueueFamilyIndices   = queueFamilies.data();
	} else {
		bufferCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	}

	// Set up Allocation info
	VmaAllocationCreateInfo bufferAI = {};
	bufferAI.usage                   = VMA_MEMORY_USAGE_AUTO;
	if (createInfo.Domain == BufferDomain::Host) {
		bufferAI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
	}

	// Create and allocate our buffer
	VmaAllocationInfo allocationInfo = {};
	{
		std::lock_guard<std::mutex> lock(_device._lock.MemoryLock);

		VkBuffer buffer = VK_NULL_HANDLE;
		const VkResult createResult =
			vmaCreateBuffer(_device._allocator, &bufferCI, &bufferAI, &buffer, &_allocation, &allocationInfo);
		if (createResult != VK_SUCCESS) {
			Log::Error("Vulkan", "Failed to create buffer: {}", vk::to_string(vk::Result(createResult)));

			throw std::runtime_error("Failed to create buffer");
		}
		_buffer = buffer;
	}

	if (_debugName.empty()) {
		Log::Trace("Vulkan", "Buffer created. ({})", Size(createInfo.Size));
	} else {
		_device.SetObjectName(_buffer, _debugName);
		vmaSetAllocationName(_device._allocator, _allocation, _debugName.c_str());
		Log::Trace("Vulkan", "Buffer \"{}\" created. ({})", _debugName, Size(createInfo.Size));
	}

	// If able, map the buffer memory
	const auto& memoryType = _device._deviceInfo.Memory.memoryTypes[allocationInfo.memoryType];
	const bool memoryIsHostVisible(memoryType.propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible);
	if (memoryIsHostVisible) {
		const VkResult mapResult = vmaMapMemory(_device._allocator, _allocation, &_mappedMemory);
		if (mapResult != VK_SUCCESS) {
			Log::Error("Vulkan", "Failed to map host-visible buffer: {}", vk::to_string(vk::Result(mapResult)));
		}
	}

	// Zero-initialize or copy initial data for our buffer
	if (initialData) {
		WriteData(initialData, createInfo.Size, 0);
	} else if (zeroInitialize) {
		FillData(0, createInfo.Size, 0);
	}
}

Buffer::~Buffer() noexcept {
	if (_internalSync) {
		_device.DestroyBufferNoLock(_buffer);
		_device.FreeAllocationNoLock(_allocation, _mappedMemory != nullptr);
	} else {
		_device.DestroyBuffer(_buffer);
		_device.FreeAllocation(_allocation, _mappedMemory != nullptr);
	}
}

void Buffer::FillData(uint8_t data, vk::DeviceSize dataSize, vk::DeviceSize offset) {
	Log::Assert(dataSize + offset <= _createInfo.Size,
	            "Vulkan::Buffer",
	            "FillData: dataSize ({}) + offset ({}) is greater than buffer size ({})",
	            dataSize,
	            offset,
	            _createInfo.Size);

	if (_mappedMemory) {
		uint8_t* mappedData = reinterpret_cast<uint8_t*>(_mappedMemory);
		std::memset(mappedData + offset, data, dataSize);
	} else {
		const std::string commandBufferName = fmt::format("{} Fill", _debugName.empty() ? "Buffer" : _debugName);
		CommandBufferHandle cmd = _device.RequestCommandBuffer(CommandBufferType::AsyncTransfer, commandBufferName);
		cmd->FillBuffer(*this, data, offset, dataSize);

		const auto stages = Buffer::UsageToStages(_createInfo.Usage);
		cmd->BufferBarrier(*this,
		                   vk::PipelineStageFlagBits2::eTransfer,
		                   vk::AccessFlagBits2::eTransferWrite,
		                   Buffer::UsageToStages(_createInfo.Usage),
		                   Buffer::UsageToAccess(_createInfo.Usage));

		_device.SubmitStaging(cmd, stages, true);
	}
}

void Buffer::WriteData(const void* data, vk::DeviceSize dataSize, vk::DeviceSize offset) {
	Log::Assert(dataSize + offset <= _createInfo.Size,
	            "Vulkan::Buffer",
	            "WriteData: dataSize ({}) + offset ({}) is greater than buffer size ({})",
	            dataSize,
	            offset,
	            _createInfo.Size);

	if (!data) { return; }

	if (_mappedMemory) {
		uint8_t* mappedData = reinterpret_cast<uint8_t*>(_mappedMemory);
		std::memcpy(mappedData + offset, data, dataSize);
	} else {
		CommandBufferHandle cmd;

		auto stagingCreateInfo = _createInfo;
		stagingCreateInfo.SetDomain(BufferDomain::Host).AddUsage(vk::BufferUsageFlagBits::eTransferSrc);

		const std::string stagingBufferName =
			_debugName.empty() ? "Staging Buffer" : fmt::format("{} [Staging]", _debugName);
		const std::string commandBufferName = fmt::format("{} Copy", stagingBufferName);

		auto stagingBuffer = _device.CreateBuffer(stagingCreateInfo, data, stagingBufferName);

		cmd = _device.RequestCommandBuffer(CommandBufferType::AsyncTransfer, commandBufferName);
		cmd->CopyBuffer(*this, *stagingBuffer);

		const auto stages = Buffer::UsageToStages(_createInfo.Usage);
		cmd->BufferBarrier(*this,
		                   vk::PipelineStageFlagBits2::eTransfer,
		                   vk::AccessFlagBits2::eTransferWrite,
		                   Buffer::UsageToStages(_createInfo.Usage),
		                   Buffer::UsageToAccess(_createInfo.Usage));

		_device.SubmitStaging(cmd, stages, true);
	}
}

vk::AccessFlags2 Buffer::UsageToAccess(vk::BufferUsageFlags usage) {
	vk::AccessFlags2 access;

	if (usage & vk::BufferUsageFlagBits::eTransferSrc) { access |= vk::AccessFlagBits2::eTransferRead; }
	if (usage & vk::BufferUsageFlagBits::eTransferDst) { access |= vk::AccessFlagBits2::eTransferWrite; }
	if (usage & (vk::BufferUsageFlagBits::eUniformTexelBuffer | vk::BufferUsageFlagBits::eUniformBuffer)) {
		access |= vk::AccessFlagBits2::eUniformRead;
	}
	if (usage & (vk::BufferUsageFlagBits::eStorageTexelBuffer | vk::BufferUsageFlagBits::eStorageBuffer)) {
		access |= vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite;
	}
	if (usage & vk::BufferUsageFlagBits::eIndexBuffer) { access |= vk::AccessFlagBits2::eIndexRead; }
	if (usage & vk::BufferUsageFlagBits::eVertexBuffer) { access |= vk::AccessFlagBits2::eVertexAttributeRead; }
	if (usage & vk::BufferUsageFlagBits::eIndirectBuffer) { access |= vk::AccessFlagBits2::eIndirectCommandRead; }
	if (usage & vk::BufferUsageFlagBits::eShaderDeviceAddress) {
		access |= vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite;
	}

	return access;
}

vk::PipelineStageFlags2 Buffer::UsageToStages(vk::BufferUsageFlags usage) {
	vk::PipelineStageFlags2 stages;

	if (usage & (vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst)) {
		stages |= vk::PipelineStageFlagBits2::eTransfer;
	}
	if (usage & (vk::BufferUsageFlagBits::eUniformTexelBuffer | vk::BufferUsageFlagBits::eStorageTexelBuffer |
	             vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eStorageBuffer |
	             vk::BufferUsageFlagBits::eShaderDeviceAddress)) {
		stages |= vk::PipelineStageFlagBits2::eAllGraphics | vk::PipelineStageFlagBits2::eComputeShader;
	}
	if (usage & vk::BufferUsageFlagBits::eIndexBuffer) { stages |= vk::PipelineStageFlagBits2::eIndexInput; }
	if (usage & vk::BufferUsageFlagBits::eVertexBuffer) { stages |= vk::PipelineStageFlagBits2::eVertexAttributeInput; }
	if (usage & vk::BufferUsageFlagBits::eIndirectBuffer) { stages |= vk::PipelineStageFlagBits2::eDrawIndirect; }

	return stages;
}
}  // namespace Vulkan
}  // namespace Luna
