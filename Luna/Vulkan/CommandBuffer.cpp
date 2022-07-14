#include "CommandBuffer.hpp"

#include "Buffer.hpp"
#include "Device.hpp"
#include "Format.hpp"
#include "Image.hpp"

namespace Luna {
namespace Vulkan {
void CommandBufferDeleter::operator()(CommandBuffer* commandBuffer) {}

CommandBuffer::CommandBuffer(Device& device,
                             vk::CommandBuffer commandBuffer,
                             CommandBufferType type,
                             uint32_t threadIndex)
		: _device(device), _commandBuffer(commandBuffer), _type(type), _threadIndex(threadIndex) {}

CommandBuffer::~CommandBuffer() noexcept {}

void CommandBuffer::End() {
	_commandBuffer.end();
}

void CommandBuffer::Barrier(vk::PipelineStageFlags srcStage,
                            vk::AccessFlags srcAccess,
                            vk::PipelineStageFlags dstStage,
                            vk::AccessFlags dstAccess) {
	const vk::MemoryBarrier barrier(srcAccess, dstAccess);
	_commandBuffer.pipelineBarrier(srcStage, dstStage, {}, barrier, nullptr, nullptr);
}

void CommandBuffer::Barrier(vk::PipelineStageFlags srcStage,
                            vk::PipelineStageFlags dstStage,
                            const std::vector<vk::MemoryBarrier>& memoryBarriers,
                            const std::vector<vk::BufferMemoryBarrier>& bufferBarriers,
                            const std::vector<vk::ImageMemoryBarrier>& imageBarriers) {
	_commandBuffer.pipelineBarrier(srcStage, dstStage, {}, memoryBarriers, bufferBarriers, imageBarriers);
}

void CommandBuffer::ImageBarrier(const Image& image,
                                 vk::ImageLayout oldLayout,
                                 vk::ImageLayout newLayout,
                                 vk::PipelineStageFlags srcStage,
                                 vk::AccessFlags srcAccess,
                                 vk::PipelineStageFlags dstStage,
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
	_commandBuffer.pipelineBarrier(srcStage, dstStage, {}, nullptr, nullptr, barrier);
}

void CommandBuffer::MipmapBarrier(const Image& image,
                                  vk::ImageLayout baseLevelLayout,
                                  vk::PipelineStageFlags srcStage,
                                  vk::AccessFlags srcAccess,
                                  bool needTopLevelBarrier) {
	auto& createInfo = image.GetCreateInfo();

	std::vector<vk::ImageMemoryBarrier> barriers;

	if (needTopLevelBarrier) {
		barriers.push_back(vk::ImageMemoryBarrier(
			srcAccess,
			vk::AccessFlagBits::eTransferRead,
			baseLevelLayout,
			vk::ImageLayout::eTransferSrcOptimal,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			image.GetImage(),
			vk::ImageSubresourceRange(FormatToAspect(createInfo.Format), 0, 1, 0, createInfo.ArrayLayers)));
	}

	barriers.push_back(vk::ImageMemoryBarrier(
		{},
		vk::AccessFlagBits::eTransferWrite,
		vk::ImageLayout::eUndefined,
		vk::ImageLayout::eTransferDstOptimal,
		VK_QUEUE_FAMILY_IGNORED,
		VK_QUEUE_FAMILY_IGNORED,
		image.GetImage(),
		vk::ImageSubresourceRange(
			FormatToAspect(createInfo.Format), 1, createInfo.MipLevels - 1, 0, createInfo.ArrayLayers)));

	Barrier(srcStage, vk::PipelineStageFlagBits::eTransfer, {}, {}, barriers);
}

void CommandBuffer::BlitImage(const Image& dst,
                              const Image& src,
                              const vk::Offset3D& dstOffset,
                              const vk::Offset3D& dstExtent,
                              const vk::Offset3D& srcOffset,
                              const vk::Offset3D& srcExtent,
                              unsigned dstLevel,
                              unsigned srcLevel,
                              unsigned dstBaseLayer,
                              uint32_t srcBaseLayer,
                              unsigned numLayers,
                              vk::Filter filter) {
	const auto AddOffset = [](const vk::Offset3D& a, const vk::Offset3D& b) -> vk::Offset3D {
		return vk::Offset3D(a.x + b.x, a.y + b.y, a.z + b.z);
	};

	const vk::ImageBlit blit(
		vk::ImageSubresourceLayers(FormatToAspect(src.GetCreateInfo().Format), srcLevel, srcBaseLayer, numLayers),
		{srcOffset, AddOffset(srcOffset, srcExtent)},
		vk::ImageSubresourceLayers(FormatToAspect(dst.GetCreateInfo().Format), dstLevel, dstBaseLayer, numLayers),
		{dstOffset, AddOffset(dstOffset, dstExtent)});
	_commandBuffer.blitImage(src.GetImage(),
	                         src.GetLayout(vk::ImageLayout::eTransferSrcOptimal),
	                         dst.GetImage(),
	                         dst.GetLayout(vk::ImageLayout::eTransferDstOptimal),
	                         blit,
	                         filter);
}

void CommandBuffer::CopyBuffer(const Buffer& dst, const Buffer& src) {
	CopyBuffer(dst, 0, src, 0, dst.GetCreateInfo().Size);
}

void CommandBuffer::CopyBuffer(
	const Buffer& dst, vk::DeviceSize dstOffset, const Buffer& src, vk::DeviceSize srcOffset, vk::DeviceSize size) {
	const vk::BufferCopy copy(srcOffset, dstOffset, size);
	_commandBuffer.copyBuffer(src.GetBuffer(), dst.GetBuffer(), copy);
}

void CommandBuffer::CopyBufferToImage(const Image& image,
                                      const Buffer& buffer,
                                      const std::vector<vk::BufferImageCopy>& blits) {
	_commandBuffer.copyBufferToImage(
		buffer.GetBuffer(), image.GetImage(), image.GetLayout(vk::ImageLayout::eTransferDstOptimal), blits);
}

void CommandBuffer::GenerateMipmaps(const Image& image) {
	auto& createInfo = image.GetCreateInfo();
	vk::Offset3D size(createInfo.Width, createInfo.Height, createInfo.Depth);
	vk::Offset3D origin(0, 0, 0);

	vk::ImageMemoryBarrier barrier(
		vk::AccessFlagBits::eTransferWrite,
		vk::AccessFlagBits::eTransferRead,
		vk::ImageLayout::eTransferDstOptimal,
		vk::ImageLayout::eTransferSrcOptimal,
		VK_QUEUE_FAMILY_IGNORED,
		VK_QUEUE_FAMILY_IGNORED,
		image.GetImage(),
		vk::ImageSubresourceRange(FormatToAspect(createInfo.Format), 0, 1, 0, createInfo.ArrayLayers));

	for (int i = 1; i < createInfo.MipLevels; ++i) {
		vk::Offset3D srcSize = size;
		size.x               = std::max(size.x >> 1, 1);
		size.y               = std::max(size.y >> 1, 1);
		size.z               = std::max(size.z >> 1, 1);

		BlitImage(image, image, origin, size, origin, srcSize, i, i - 1, 0, 0, createInfo.ArrayLayers, vk::Filter::eLinear);

		barrier.subresourceRange.baseMipLevel = i;
		Barrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, {}, {barrier});
	}
}
}  // namespace Vulkan
}  // namespace Luna
