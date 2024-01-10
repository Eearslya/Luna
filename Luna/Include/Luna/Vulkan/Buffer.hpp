#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
struct BufferCreateInfo {
	constexpr BufferCreateInfo() noexcept = default;
	constexpr BufferCreateInfo(BufferDomain domain, vk::DeviceSize size) noexcept : Domain(domain), Size(size) {}

	constexpr BufferCreateInfo& SetDomain(BufferDomain domain) noexcept {
		Domain = domain;

		return *this;
	}
	constexpr BufferCreateInfo& SetSize(vk::DeviceSize size) noexcept {
		Size = size;

		return *this;
	}
	constexpr BufferCreateInfo& SetFlags(BufferCreateFlags flags) noexcept {
		Flags = flags;

		return *this;
	}
	constexpr BufferCreateInfo& AddFlags(BufferCreateFlags flags) noexcept {
		Flags |= flags;

		return *this;
	}
	constexpr BufferCreateInfo& ZeroInitialize() noexcept {
		Flags |= BufferCreateFlagBits::ZeroInitialize;

		return *this;
	}

	BufferDomain Domain = BufferDomain::Device;
	vk::DeviceSize Size = 0;
	BufferCreateFlags Flags;
};

struct BufferDeleter {
	void operator()(Buffer* buffer);
};

class Buffer : public VulkanObject<Buffer, BufferDeleter>, public Cookie, public InternalSyncEnabled {
	friend class ObjectPool<Buffer>;
	friend struct BufferDeleter;

 public:
	~Buffer() noexcept;

	[[nodiscard]] const VmaAllocation& GetAllocation() const noexcept {
		return _allocation;
	}
	[[nodiscard]] vk::Buffer GetBuffer() const noexcept {
		return _buffer;
	}
	[[nodiscard]] const BufferCreateInfo& GetCreateInfo() const noexcept {
		return _createInfo;
	}
	[[nodiscard]] vk::DeviceAddress GetDeviceAddress() const noexcept {
		return _deviceAddress;
	}
	[[nodiscard]] vk::DeviceSize GetSize() const noexcept {
		return _createInfo.Size;
	}

	template <typename T = void>
	[[nodiscard]] T* Map() const {
		return reinterpret_cast<T*>(_mappedMemory);
	}

	void FillData(uint8_t data, vk::DeviceSize dataSize, vk::DeviceSize offset = 0);
	void WriteData(const void* data, vk::DeviceSize dataSize, vk::DeviceSize offset = 0);

 private:
	Buffer(Device& device, const BufferCreateInfo& createInfo, const void* initialData, const std::string& debugName);

	Device& _device;
	BufferCreateInfo _createInfo;
	std::string _debugName;

	vk::Buffer _buffer;
	VmaAllocation _allocation;
	vk::DeviceAddress _deviceAddress = 0;
	void* _mappedMemory              = nullptr;
};
}  // namespace Vulkan
}  // namespace Luna
