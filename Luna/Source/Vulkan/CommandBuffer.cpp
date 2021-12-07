#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Format.hpp>
#include <Luna/Vulkan/Image.hpp>

namespace Luna {
namespace Vulkan {
void CommandBufferDeleter::operator()(CommandBuffer* buffer) {
	buffer->_device.ReleaseCommandBuffer({}, buffer);
}

CommandBuffer::CommandBuffer(Device& device,
                             vk::CommandBuffer commandBuffer,
                             CommandBufferType type,
                             uint32_t threadIndex)
		: _device(device), _commandBuffer(commandBuffer), _commandBufferType(type), _threadIndex(threadIndex) {}

CommandBuffer::~CommandBuffer() noexcept {}

void CommandBuffer::Begin() {
	const vk::CommandBufferBeginInfo cmdBI(vk::CommandBufferUsageFlagBits::eOneTimeSubmit, nullptr);
	_commandBuffer.begin(cmdBI);
}

void CommandBuffer::End() {
	_commandBuffer.end();
}

void CommandBuffer::Barrier(vk::PipelineStageFlags srcStages,
                            vk::AccessFlags srcAccess,
                            vk::PipelineStageFlags dstStages,
                            vk::AccessFlags dstAccess) {
	const vk::MemoryBarrier barrier(srcAccess, dstAccess);
	_commandBuffer.pipelineBarrier(srcStages, dstStages, {}, barrier, nullptr, nullptr);
}

void CommandBuffer::Barrier(vk::PipelineStageFlags srcStages,
                            vk::PipelineStageFlags dstStages,
                            const vk::ArrayProxy<const vk::MemoryBarrier>& barriers,
                            const vk::ArrayProxy<const vk::BufferMemoryBarrier>& buffers,
                            const vk::ArrayProxy<const vk::ImageMemoryBarrier>& images) {
	_commandBuffer.pipelineBarrier(srcStages, dstStages, {}, barriers, buffers, images);
}

void CommandBuffer::BlitImage(Image& dst,
                              Image& src,
                              const vk::Offset3D& dstOffset,
                              const vk::Extent3D& dstExtent,
                              const vk::Offset3D& srcOffset,
                              const vk::Extent3D& srcExtent,
                              uint32_t dstLevel,
                              uint32_t srcLevel,
                              uint32_t dstBaseLayer,
                              uint32_t srcBaseLayer,
                              uint32_t layerCount,
                              vk::Filter filter) {
	const auto AddOffset = [](const vk::Offset3D& a, const vk::Extent3D& b) -> vk::Offset3D {
		return {
			a.x + static_cast<int32_t>(b.width), a.y + static_cast<int32_t>(b.height), a.z + static_cast<int32_t>(b.depth)};
	};

	for (uint32_t i = 0; i < layerCount; ++i) {
		const vk::ImageBlit blit(
			vk::ImageSubresourceLayers(FormatToAspect(src.GetCreateInfo().Format), srcLevel, srcBaseLayer + i, 1),
			{srcOffset, AddOffset(srcOffset, srcExtent)},
			vk::ImageSubresourceLayers(FormatToAspect(dst.GetCreateInfo().Format), dstLevel, dstBaseLayer + i, 1),
			{dstOffset, AddOffset(dstOffset, dstExtent)});
		_commandBuffer.blitImage(src.GetImage(),
		                         src.GetLayout(vk::ImageLayout::eTransferSrcOptimal),
		                         dst.GetImage(),
		                         dst.GetLayout(vk::ImageLayout::eTransferDstOptimal),
		                         blit,
		                         filter);
	}
}

void CommandBuffer::CopyBuffer(Buffer& dst, Buffer& src) {
	CopyBuffer(dst, 0, src, 0, dst.GetCreateInfo().Size);
}

void CommandBuffer::CopyBuffer(
	Buffer& dst, vk::DeviceSize dstOffset, Buffer& src, vk::DeviceSize srcOffset, vk::DeviceSize bytes) {
	const vk::BufferCopy region(srcOffset, dstOffset, bytes);
	_commandBuffer.copyBuffer(src.GetBuffer(), dst.GetBuffer(), region);
}

void CommandBuffer::CopyBufferToImage(Image& dst, Buffer& src, const std::vector<vk::BufferImageCopy>& copies) {
	_commandBuffer.copyBufferToImage(
		src.GetBuffer(), dst.GetImage(), dst.GetLayout(vk::ImageLayout::eTransferDstOptimal), copies);
}

void CommandBuffer::GenerateMipmaps(Image& image,
                                    vk::ImageLayout baseLayout,
                                    vk::PipelineStageFlags srcStage,
                                    vk::AccessFlags srcAccess,
                                    bool needTopLevelBarrier) {
	const auto& createInfo = image.GetCreateInfo();

	// We start with the top and bottom barriers, which will prepare the mips and finalize them.
	{
		std::array<vk::ImageMemoryBarrier, 2> barriers;
		for (uint32_t i = 0; i < 2; ++i) {
			const bool top = i == 0;
			barriers[i] =
				vk::ImageMemoryBarrier(top ? srcAccess : vk::AccessFlags(),
			                         top ? vk::AccessFlagBits::eTransferRead : vk::AccessFlagBits::eTransferWrite,
			                         top ? baseLayout : vk::ImageLayout::eUndefined,
			                         top ? vk::ImageLayout::eTransferSrcOptimal : vk::ImageLayout::eTransferDstOptimal,
			                         VK_QUEUE_FAMILY_IGNORED,
			                         VK_QUEUE_FAMILY_IGNORED,
			                         image.GetImage(),
			                         vk::ImageSubresourceRange(FormatToAspect(createInfo.Format),
			                                                   top ? 0 : 1,
			                                                   top ? 1 : createInfo.MipLevels - 1,
			                                                   0,
			                                                   createInfo.ArrayLayers));
		}

		if (needTopLevelBarrier) {
			Barrier(srcStage, vk::PipelineStageFlagBits::eTransfer, nullptr, nullptr, barriers);
		} else {
			Barrier(srcStage, vk::PipelineStageFlagBits::eTransfer, nullptr, nullptr, barriers[1]);
		}
	}

	// Now we go down each mip level and blit to generate our mips.
	vk::Extent3D size = createInfo.Extent;
	const vk::Offset3D origin;

	vk::ImageMemoryBarrier barrier(
		vk::AccessFlagBits::eTransferWrite,
		vk::AccessFlagBits::eTransferRead,
		vk::ImageLayout::eTransferDstOptimal,
		vk::ImageLayout::eTransferSrcOptimal,
		VK_QUEUE_FAMILY_IGNORED,
		VK_QUEUE_FAMILY_IGNORED,
		image.GetImage(),
		vk::ImageSubresourceRange(FormatToAspect(createInfo.Format), 0, 1, 0, createInfo.ArrayLayers));
	for (uint32_t i = 1; i < createInfo.MipLevels; ++i) {
		const vk::Extent3D srcSize = size;
		size.width                 = std::max(size.width >> 1u, 1u);
		size.height                = std::max(size.height >> 1u, 1u);
		size.depth                 = std::max(size.depth >> 1u, 1u);

		BlitImage(image, image, origin, size, origin, srcSize, i, i - 1, 0, 0, createInfo.ArrayLayers, vk::Filter::eLinear);

		barrier.subresourceRange.baseMipLevel = i;

		Barrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, nullptr, nullptr, barrier);
	}
}

void CommandBuffer::ImageBarrier(Image& image,
                                 vk::ImageLayout oldLayout,
                                 vk::ImageLayout newLayout,
                                 vk::PipelineStageFlags srcStages,
                                 vk::AccessFlags srcAccess,
                                 vk::PipelineStageFlags dstStages,
                                 vk::AccessFlags dstAccess) {
	const vk::ImageMemoryBarrier barrier(srcAccess,
	                                     dstAccess,
	                                     oldLayout,
	                                     newLayout,
	                                     VK_QUEUE_FAMILY_IGNORED,
	                                     VK_QUEUE_FAMILY_IGNORED,
	                                     image.GetImage(),
	                                     vk::ImageSubresourceRange(FormatToAspect(image.GetCreateInfo().Format),
	                                                               0,
	                                                               image.GetCreateInfo().MipLevels,
	                                                               0,
	                                                               image.GetCreateInfo().ArrayLayers));
	_commandBuffer.pipelineBarrier(srcStages, dstStages, {}, nullptr, nullptr, barrier);
}
}  // namespace Vulkan
}  // namespace Luna
