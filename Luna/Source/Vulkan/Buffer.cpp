#include <Luna/Core/Log.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/Device.hpp>

namespace Luna {
namespace Vulkan {
void BufferDeleter::operator()(Buffer* buffer) {
	buffer->_device.DestroyBuffer({}, buffer);
}

Buffer::Buffer(Device& device, const BufferCreateInfo& createInfo)
		: Cookie(device), _device(device), _createInfo(createInfo) {
	auto dev = _device.GetDevice();

	const vk::BufferCreateInfo bufferCI({}, _createInfo.Size, _createInfo.Usage, vk::SharingMode::eExclusive, nullptr);
	// VMA_ALLOCATION_CREATE_MAPPED_BIT is safe to use even on non-mappable buffers, as VMA will simply ignore it.
	VmaAllocationCreateInfo bufferAI = {.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT};
	switch (_createInfo.Domain) {
		case BufferDomain::Device:
			bufferAI.usage = VMA_MEMORY_USAGE_GPU_ONLY;
			break;

		case BufferDomain::Host:
		default:
			bufferAI.usage          = VMA_MEMORY_USAGE_CPU_TO_GPU;
			bufferAI.preferredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
			break;
	}

	Log::Trace("[Vulkan::Buffer] Creating new Buffer.");

	VkBuffer buffer;
	VmaAllocation allocation;
	VmaAllocationInfo allocationInfo;
	const vk::Result createResult =
		static_cast<vk::Result>(vmaCreateBuffer(_device.GetAllocator(),
	                                          reinterpret_cast<const VkBufferCreateInfo*>(&bufferCI),
	                                          &bufferAI,
	                                          &buffer,
	                                          &allocation,
	                                          &allocationInfo));
	if (createResult != vk::Result::eSuccess) {
		// Use vulkan.hpp's ResultValue to throw the proper exception.
		vk::createResultValue(createResult, "vmaCreateBuffer");
	}

	_buffer           = buffer;
	_allocation       = allocation;
	_mapped           = allocationInfo.pMappedData;
	_memoryProperties = _device.GetGPUInfo().Memory.memoryTypes[allocationInfo.memoryType].propertyFlags;
}

Buffer::~Buffer() noexcept {
	auto dev = _device.GetDevice();

	if (_buffer) { dev.destroyBuffer(_buffer); }
	if (_allocation) { vmaFreeMemory(_device.GetAllocator(), _allocation); }
}

void* Buffer::Map() {
	return _mapped;
}

void Buffer::Unmap() {}
}  // namespace Vulkan
}  // namespace Luna
