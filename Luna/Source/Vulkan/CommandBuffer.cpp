#include <Luna/Core/Log.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Format.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/RenderPass.hpp>
#include <Luna/Vulkan/Shader.hpp>

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

void CommandBuffer::BeginRenderPass(const RenderPassInfo& info) {
	_framebuffer      = &_device.RequestFramebuffer({}, info);
	_actualRenderPass = &_device.RequestRenderPass(Badge<CommandBuffer>{}, info, false);

	uint32_t clearValueCount = 0;
	std::array<vk::ClearValue, MaxColorAttachments + 1> clearValues;
	for (uint32_t i = 0; i < info.ColorAttachmentCount; ++i) {
		if (info.ClearAttachments & (1u << i)) {
			clearValues[i].setColor(info.ClearColors[i]);
			clearValueCount = i + 1;
		}
		if (info.ColorAttachments[i]->GetImage().IsSwapchainImage()) {
			_swapchainStages |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
		}
	}
	if (info.DepthStencilAttachment && (info.DSOps & DepthStencilOpBits::ClearDepthStencil)) {
		clearValues[info.ColorAttachmentCount].setDepthStencil(info.ClearDepthStencil);
		clearValueCount = info.ColorAttachmentCount + 1;
	}

	SetViewportScissor();

	const vk::RenderPassBeginInfo rpBI(
		_actualRenderPass->GetRenderPass(), _framebuffer->GetFramebuffer(), _scissor, clearValueCount, clearValues.data());
	_commandBuffer.beginRenderPass(rpBI, vk::SubpassContents::eInline);
}

void CommandBuffer::EndRenderPass() {
	_commandBuffer.endRenderPass();

	_framebuffer      = nullptr;
	_actualRenderPass = nullptr;
}

void CommandBuffer::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
	if (FlushRenderState(true)) { _commandBuffer.draw(vertexCount, instanceCount, firstVertex, firstInstance); }
}

void CommandBuffer::SetProgram(const Program* program) {
	if (_pipelineCompileInfo.Program == program) { return; }

	_pipelineCompileInfo.Program = program;
	_pipeline                    = VK_NULL_HANDLE;

	_dirty |= CommandBufferDirtyFlagBits::Pipeline | CommandBufferDirtyFlagBits::DynamicState;
	if (!program) { return; }

	if (!_pipelineLayout) {
		_dirty |= CommandBufferDirtyFlagBits::PushConstants;
		_dirtyDescriptorSets = ~0u;

	} else if (program->GetPipelineLayout()->GetHash() != _programLayout->GetHash()) {
		auto& newLayout = program->GetPipelineLayout()->GetResourceLayout();
		auto& oldLayout = _programLayout->GetResourceLayout();

		if (newLayout.PushConstantLayoutHash != oldLayout.PushConstantLayoutHash) {
			_dirty |= CommandBufferDirtyFlagBits::PushConstants;
			_dirtyDescriptorSets = ~0u;
		} else {
			auto* newPipelineLayout = program->GetPipelineLayout();
			for (uint32_t set = 0; set < MaxDescriptorSets; ++set) {}
		}
	}

	_programLayout  = _pipelineCompileInfo.Program->GetPipelineLayout();
	_pipelineLayout = _programLayout->GetPipelineLayout();
}

vk::Pipeline CommandBuffer::BuildGraphicsPipeline(bool synchronous) {
	const vk::PipelineViewportStateCreateInfo viewport({}, 1, nullptr, 1, nullptr);
	const std::vector<vk::DynamicState> dynamicStates{vk::DynamicState::eScissor, vk::DynamicState::eViewport};
	const vk::PipelineDynamicStateCreateInfo dynamic({}, dynamicStates);
	const vk::PipelineColorBlendAttachmentState blend1(VK_FALSE,
	                                                   vk::BlendFactor::eOne,
	                                                   vk::BlendFactor::eZero,
	                                                   vk::BlendOp::eAdd,
	                                                   vk::BlendFactor::eOne,
	                                                   vk::BlendFactor::eZero,
	                                                   vk::BlendOp::eAdd,
	                                                   vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
	                                                     vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
	const vk::PipelineColorBlendStateCreateInfo blending(
		{}, VK_FALSE, vk::LogicOp::eCopy, blend1, {1.0f, 1.0f, 1.0f, 1.0f});
	const vk::PipelineVertexInputStateCreateInfo vertexInput({}, nullptr, nullptr);
	const vk::PipelineInputAssemblyStateCreateInfo assembly({}, vk::PrimitiveTopology::eTriangleList, VK_FALSE);
	const vk::PipelineMultisampleStateCreateInfo multisample(
		{}, vk::SampleCountFlagBits::e1, VK_FALSE, 0.0f, nullptr, VK_FALSE, VK_FALSE);
	const vk::PipelineRasterizationStateCreateInfo rasterizer({},
	                                                          VK_FALSE,
	                                                          VK_FALSE,
	                                                          vk::PolygonMode::eFill,
	                                                          vk::CullModeFlagBits::eNone,
	                                                          vk::FrontFace::eCounterClockwise,
	                                                          VK_FALSE,
	                                                          0.0f,
	                                                          0.0f,
	                                                          0.0f,
	                                                          1.0f);
	std::vector<vk::PipelineShaderStageCreateInfo> stages;
	stages.push_back(
		vk::PipelineShaderStageCreateInfo({},
	                                    vk::ShaderStageFlagBits::eVertex,
	                                    _pipelineCompileInfo.Program->GetShader(ShaderStage::Vertex)->GetShaderModule(),
	                                    "main",
	                                    nullptr));
	stages.push_back(
		vk::PipelineShaderStageCreateInfo({},
	                                    vk::ShaderStageFlagBits::eFragment,
	                                    _pipelineCompileInfo.Program->GetShader(ShaderStage::Fragment)->GetShaderModule(),
	                                    "main",
	                                    nullptr));

	const vk::GraphicsPipelineCreateInfo pipelineCI({},
	                                                stages,
	                                                &vertexInput,
	                                                &assembly,
	                                                nullptr,
	                                                &viewport,
	                                                &rasterizer,
	                                                &multisample,
	                                                nullptr,
	                                                &blending,
	                                                &dynamic,
	                                                _pipelineLayout,
	                                                _actualRenderPass->GetRenderPass(),
	                                                0,
	                                                VK_NULL_HANDLE,
	                                                0);
	Log::Trace("[Vulkan::CommandBuffer] Creating new Pipeline.");
	const auto pipelineResult = _device.GetDevice().createGraphicsPipeline(VK_NULL_HANDLE, pipelineCI);
	_pipelineCompileInfo.Program->SetPipeline(pipelineResult.value);

	return pipelineResult.value;
}

bool CommandBuffer::FlushGraphicsPipeline(bool synchronous) {
	_pipeline = _pipelineCompileInfo.Program->GetPipeline();
	if (!_pipeline) { _pipeline = BuildGraphicsPipeline(synchronous); }
	return bool(_pipeline);
}

bool CommandBuffer::FlushRenderState(bool synchronous) {
	if (!_pipelineCompileInfo.Program) { return false; }
	if (!_pipeline) { _dirty |= CommandBufferDirtyFlagBits::Pipeline; }

	if (_dirty & (CommandBufferDirtyFlagBits::StaticState | CommandBufferDirtyFlagBits::Pipeline |
	              CommandBufferDirtyFlagBits::StaticVertex)) {
		vk::Pipeline oldPipeline = _pipeline;
		if (!FlushGraphicsPipeline(synchronous)) { return false; }

		if (oldPipeline != _pipeline) {
			_commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, _pipeline);
			_dirty |= CommandBufferDirtyFlagBits::DynamicState;
		}
	}
	_dirty &= ~(CommandBufferDirtyFlagBits::StaticState | CommandBufferDirtyFlagBits::Pipeline |
	            CommandBufferDirtyFlagBits::StaticVertex);

	if (!_pipeline) { return false; }

	if (_dirty & CommandBufferDirtyFlagBits::Viewport) { _commandBuffer.setViewport(0, _viewport); }
	_dirty &= ~CommandBufferDirtyFlagBits::Viewport;

	if (_dirty & CommandBufferDirtyFlagBits::Scissor) { _commandBuffer.setScissor(0, _scissor); }
	_dirty &= ~CommandBufferDirtyFlagBits::Scissor;

	return true;
}

void CommandBuffer::SetViewportScissor() {
	const auto& rpInfo   = _actualRenderPass->GetRenderPassInfo();
	const auto& fbExtent = _framebuffer->GetExtent();

	_scissor               = rpInfo.RenderArea;
	_scissor.offset.x      = std::min(fbExtent.width, static_cast<uint32_t>(_scissor.offset.x));
	_scissor.offset.y      = std::min(fbExtent.height, static_cast<uint32_t>(_scissor.offset.y));
	_scissor.extent.width  = std::min(fbExtent.width - _scissor.offset.x, _scissor.extent.width);
	_scissor.extent.height = std::min(fbExtent.height - _scissor.offset.y, _scissor.extent.height);

	_viewport = vk::Viewport{static_cast<float>(_scissor.offset.x),
	                         static_cast<float>(_scissor.offset.y),
	                         static_cast<float>(_scissor.extent.width),
	                         static_cast<float>(_scissor.extent.height),
	                         0.0f,
	                         1.0f};
}
}  // namespace Vulkan
}  // namespace Luna
