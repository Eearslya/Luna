#include <Luna/Utility/BitOps.hpp>
#include <Luna/Utility/SpinLock.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/DescriptorSet.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/Shader.hpp>

namespace Luna {
namespace Vulkan {
static uint32_t CombinedSpecConstantMask(const DeferredPipelineCompile& pipelineState) {
	return pipelineState.PotentialStaticState.SpecConstantMask |
	       (pipelineState.PotentialStaticState.InternalSpecConstantMask << MaxUserSpecConstants);
}

Hash DeferredPipelineCompile::GetComputeHash() const {
	Hasher h;
	h(Program->GetHash());
	h(PipelineLayout->GetHash());

	auto& layout                  = PipelineLayout->GetResourceLayout();
	uint32_t combinedSpecConstant = layout.CombinedSpecConstantMask;
	combinedSpecConstant &= CombinedSpecConstantMask(*this);
	h(combinedSpecConstant);
	ForEachBit(combinedSpecConstant, [&](uint32_t bit) { h(PotentialStaticState.SpecConstants[bit]); });

	if (StaticState.SubgroupControlSize) {
		h(int32_t(1));
		h(StaticState.SubgroupMinimumSizeLog2);
		h(StaticState.SubgroupMaximumSizeLog2);
		h(StaticState.SubgroupFullGroup);
		h(SubgroupSizeTag);
	} else {
		h(int32_t(0));
	}

	CachedHash = h.Get();

	return CachedHash;
}

Hash DeferredPipelineCompile::GetHash(uint32_t& activeVBOs) const {
	return {};
}

void CommandBufferDeleter::operator()(CommandBuffer* commandBuffer) {
	commandBuffer->_device._commandBufferPool.Free(commandBuffer);
}

CommandBuffer::CommandBuffer(Device& device,
                             CommandBufferType type,
                             vk::CommandBuffer commandBuffer,
                             uint32_t threadIndex,
                             const std::string& debugName)
		: _device(device), _type(type), _commandBuffer(commandBuffer), _threadIndex(threadIndex), _debugName(debugName) {
	_device.SetObjectName(_commandBuffer, debugName);
	BeginCompute();
	SetOpaqueState();
	memset(&_pipelineState.StaticState, 0, sizeof(_pipelineState.StaticState));
	memset(&_resources, 0, sizeof(_resources));

	_device._lock.ReadOnlyCache.LockRead();
}

CommandBuffer::~CommandBuffer() noexcept {
	_device._lock.ReadOnlyCache.UnlockRead();
}

void CommandBuffer::Begin() {
	const vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit, nullptr);
	_commandBuffer.begin(beginInfo);
}

void CommandBuffer::End() {
	_commandBuffer.end();
}

void CommandBuffer::Barrier(const vk::DependencyInfo& dependency) {
	_commandBuffer.pipelineBarrier2(dependency);
}

void CommandBuffer::BufferBarrier(const Buffer& buffer,
                                  vk::PipelineStageFlags2 srcStages,
                                  vk::AccessFlags2 srcAccess,
                                  vk::PipelineStageFlags2 dstStages,
                                  vk::AccessFlags2 dstAccess) {
	const vk::BufferMemoryBarrier2 barrier(srcStages,
	                                       srcAccess,
	                                       dstStages,
	                                       dstAccess,
	                                       VK_QUEUE_FAMILY_IGNORED,
	                                       VK_QUEUE_FAMILY_IGNORED,
	                                       buffer.GetBuffer(),
	                                       0,
	                                       VK_WHOLE_SIZE);
	const vk::DependencyInfo dependency({}, nullptr, barrier, nullptr);
	Barrier(dependency);
}

void CommandBuffer::CopyBuffer(const Buffer& dst, const Buffer& src) {
	CopyBuffer(dst, 0, src, 0, src.GetCreateInfo().Size);
}

void CommandBuffer::CopyBuffer(const Buffer& dst, const Buffer& src, const std::vector<vk::BufferCopy>& copies) {
	_commandBuffer.copyBuffer(src.GetBuffer(), dst.GetBuffer(), copies);
}

void CommandBuffer::CopyBuffer(
	const Buffer& dst, vk::DeviceSize dstOffset, const Buffer& src, vk::DeviceSize srcOffset, vk::DeviceSize size) {
	const vk::BufferCopy copy(srcOffset, dstOffset, size);
	_commandBuffer.copyBuffer(src.GetBuffer(), dst.GetBuffer(), copy);
}

void CommandBuffer::FillBuffer(const Buffer& dst, uint8_t value) {
	FillBuffer(dst, value, 0, VK_WHOLE_SIZE);
}

void CommandBuffer::FillBuffer(const Buffer& dst, uint8_t value, vk::DeviceSize offset, vk::DeviceSize size) {
	_commandBuffer.fillBuffer(dst.GetBuffer(), offset, size, value);
}

void CommandBuffer::PushConstants(size_t size, const void* data, vk::DeviceSize offset) {
	memcpy(&_resources.PushConstantData[offset], data, size);
	_dirty |= CommandBufferDirtyFlagBits::PushConstants;
}

void CommandBuffer::SetStorageBuffer(uint32_t set, uint32_t binding, const Buffer& buffer) {
	SetStorageBuffer(set, binding, buffer, 0, buffer.GetCreateInfo().Size);
}

void CommandBuffer::SetStorageBuffer(
	uint32_t set, uint32_t binding, const Buffer& buffer, vk::DeviceSize offset, vk::DeviceSize range) {
	auto& bind = _resources.Bindings[set][binding];

	if (buffer.GetCookie() == bind.Cookie && bind.Buffer.offset == offset && bind.Buffer.range == range) { return; }

	bind.Buffer          = vk::DescriptorBufferInfo(buffer.GetBuffer(), offset, range);
	bind.DynamicOffset   = 0;
	bind.Cookie          = buffer.GetCookie();
	bind.SecondaryCookie = 0;

	_dirtySets |= 1u << set;
}

void CommandBuffer::Dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) {
	if (FlushComputeState(true)) { _commandBuffer.dispatch(groupsX, groupsY, groupsZ); }
}

void CommandBuffer::BeginRenderPass(const RenderPassInfo& rpInfo, vk::SubpassContents contents) {
	_framebuffer                        = &_device.RequestFramebuffer(rpInfo);
	_pipelineState.CompatibleRenderPass = &_framebuffer->GetCompatibleRenderPass();
	_actualRenderPass                   = &_device.RequestRenderPass(rpInfo, false);
	_pipelineState.SubpassIndex         = 0;

	_framebufferAttachments.fill(nullptr);
	for (uint32_t i = 0; i < rpInfo.ColorAttachmentCount; ++i) {
		_framebufferAttachments[i] = rpInfo.ColorAttachments[i];
	}
	if (rpInfo.DepthStencilAttachment) {
		_framebufferAttachments[rpInfo.ColorAttachmentCount] = rpInfo.DepthStencilAttachment;
	}

	SetViewportScissor(rpInfo, _framebuffer);

	uint32_t clearValueCount = 0;
	std::array<vk::ClearValue, MaxColorAttachments + 1> clearValues;
	for (uint32_t i = 0; i < rpInfo.ColorAttachmentCount; ++i) {
		if (rpInfo.ClearAttachmentMask & (1u << i)) {
			clearValues[i].color = rpInfo.ClearColors[i];
			clearValueCount      = i + 1;
		}

		if (rpInfo.ColorAttachments[i]->GetImage().IsSwapchainImage()) {
			_swapchainStages |= vk::PipelineStageFlagBits2::eColorAttachmentOutput;
		}
	}
	if (rpInfo.DepthStencilAttachment && (rpInfo.Flags & RenderPassFlagBits::ClearDepthStencil)) {
		clearValues[rpInfo.ColorAttachmentCount].depthStencil = rpInfo.ClearDepthStencil;
		clearValueCount                                       = rpInfo.ColorAttachmentCount + 1;
	}

	const vk::RenderPassBeginInfo rpBI(
		_actualRenderPass->GetRenderPass(), _framebuffer->GetFramebuffer(), _scissor, clearValueCount, clearValues.data());
	_commandBuffer.beginRenderPass(rpBI, contents);
	_currentContents = contents;

	BeginGraphics();
}

void CommandBuffer::NextSubpass(vk::SubpassContents contents) {
	_pipelineState.SubpassIndex++;
	_commandBuffer.nextSubpass(contents);
	_currentContents = contents;

	BeginGraphics();
}

void CommandBuffer::EndRenderPass() {
	_commandBuffer.endRenderPass();

	_framebuffer                        = nullptr;
	_actualRenderPass                   = nullptr;
	_pipelineState.CompatibleRenderPass = nullptr;

	BeginCompute();
}

void CommandBuffer::SetOpaqueState() {
	ClearRenderState();

	auto& state            = _pipelineState.StaticState;
	state.FrontFace        = int(vk::FrontFace::eCounterClockwise);
	state.CullMode         = int(vk::CullModeFlagBits::eBack);
	state.BlendEnable      = 1;
	state.DepthTest        = 1;
	state.DepthCompare     = int(vk::CompareOp::eLessOrEqual);
	state.DepthWrite       = 1;
	state.DepthBiasEnable  = 0;
	state.PrimitiveRestart = 0;
	state.StencilTest      = 0;
	state.Topology         = int(vk::PrimitiveTopology::eTriangleList);
	state.WriteMask        = ~0u;

	_dirty |= CommandBufferDirtyFlagBits::StaticState;
}

void CommandBuffer::SetProgram(Program* program) {
	if (_pipelineState.Program == program) { return; }

	_pipelineState.Program = program;
	_currentPipeline       = {};

	_dirty |= CommandBufferDirtyFlagBits::Pipeline;
	if (!program) { return; }

	if (!_pipelineState.PipelineLayout) {
		_dirty |= CommandBufferDirtyFlagBits::PushConstants;
		_dirtySets = ~0u;
	} else if (program->GetPipelineLayout()->GetHash() != _pipelineState.PipelineLayout->GetHash()) {
		auto& newLayout = program->GetPipelineLayout()->GetResourceLayout();
		auto& oldLayout = _pipelineState.PipelineLayout->GetResourceLayout();

		if (newLayout.PushConstantLayoutHash != oldLayout.PushConstantLayoutHash) {
			_dirty |= CommandBufferDirtyFlagBits::PushConstants;
			_dirtySets = ~0u;
		} else {
			auto* newPipelineLayout = program->GetPipelineLayout();
			for (uint32_t set = 0; set < MaxDescriptorSets; ++set) {
				if (newPipelineLayout->GetAllocator(set) != _pipelineState.PipelineLayout->GetAllocator(set)) {
					_dirtySets |= ~((1u << set) - 1);
					break;
				}
			}
		}
	}

	_pipelineState.PipelineLayout = program->GetPipelineLayout();
	_pipelineLayout               = _pipelineState.PipelineLayout->GetPipelineLayout();
}

void CommandBuffer::BeginCompute() {
	_isCompute = true;
	BeginContext();
}

void CommandBuffer::BeginContext() {
	_dirty                                                       = ~0u;
	_dirtySets                                                   = ~0u;
	_dirtyVBOs                                                   = ~0u;
	_currentPipeline                                             = {};
	_pipelineLayout                                              = nullptr;
	_pipelineState.PipelineLayout                                = nullptr;
	_pipelineState.Program                                       = nullptr;
	_pipelineState.PotentialStaticState.SpecConstantMask         = 0;
	_pipelineState.PotentialStaticState.InternalSpecConstantMask = 0;
	for (uint32_t set = 0; set < MaxDescriptorSets; ++set) {
		for (uint32_t binding = 0; binding < MaxDescriptorBindings; ++binding) {
			_resources.Bindings[set][binding].Cookie          = 0;
			_resources.Bindings[set][binding].SecondaryCookie = 0;
		}
	}
	_vertexBindings.Buffers.fill(VK_NULL_HANDLE);
	memset(&_indexState, 0, sizeof(_indexState));
}

void CommandBuffer::BeginGraphics() {
	_isCompute = false;
	BeginContext();
}

void CommandBuffer::BindPipeline(vk::PipelineBindPoint bindPoint,
                                 vk::Pipeline pipeline,
                                 CommandBufferDirtyFlags activeDynamicState) {
	_commandBuffer.bindPipeline(bindPoint, pipeline);
	_dirty |= (~activeDynamicState) & CommandBufferDirtyFlagBits::Dynamic;
}

Pipeline CommandBuffer::BuildComputePipeline(bool synchronous) {
	RWSpinLockReadHolder guard(_device._lock.ReadOnlyCache);

	if (!synchronous) { return {}; }

	auto& shader = *_pipelineState.Program->GetShader(ShaderStage::Compute);

	vk::PipelineShaderStageCreateInfo stage({}, vk::ShaderStageFlagBits::eCompute, shader.GetShader(), "main", nullptr);

	vk::SpecializationInfo specInfo = {};
	std::array<vk::SpecializationMapEntry, MaxSpecConstants> specMap;
	std::array<uint32_t, MaxSpecConstants> specConstants;
	const auto specMask = _pipelineState.PipelineLayout->GetResourceLayout().CombinedSpecConstantMask &
	                      CombinedSpecConstantMask(_pipelineState);
	if (specMask) {
		stage.pSpecializationInfo = &specInfo;
		specInfo.pData            = specConstants.data();
		specInfo.pMapEntries      = specMap.data();

		ForEachBit(specMask, [&](uint32_t bit) {
			auto& entry = specMap[specInfo.mapEntryCount];
			entry       = vk::SpecializationMapEntry(bit, sizeof(uint32_t) * specInfo.mapEntryCount, sizeof(uint32_t));
			specConstants[specInfo.mapEntryCount] = _pipelineState.PotentialStaticState.SpecConstants[bit];
			specInfo.mapEntryCount++;
		});
		specInfo.dataSize = specInfo.mapEntryCount * sizeof(uint32_t);
	}

	const vk::ComputePipelineCreateInfo pipelineCI({}, stage, _pipelineState.PipelineLayout->GetPipelineLayout());
	const auto result = _device.GetDevice().createComputePipeline(nullptr, pipelineCI);
	if (result.result != vk::Result::eSuccess) {
		Log::Error("CommandBuffer", "Failed to create compute pipeline: {}", vk::to_string(result.result));

		return {};
	}

	auto returnPipeline = _pipelineState.Program->AddPipeline(_pipelineState.CachedHash, {result.value, 0});
	if (returnPipeline.Pipeline != result.value) { _device.GetDevice().destroyPipeline(result.value); }

	return returnPipeline;
}

void CommandBuffer::ClearRenderState() {
	memset(&_pipelineState.StaticState, 0, sizeof(_pipelineState.StaticState));
}

bool CommandBuffer::FlushComputePipeline(bool synchronous) {
	const auto pipelineHash = _pipelineState.GetComputeHash();
	_currentPipeline        = _pipelineState.Program->GetPipeline(pipelineHash);
	if (!_currentPipeline.Pipeline) { _currentPipeline = BuildComputePipeline(synchronous); }

	return bool(_currentPipeline.Pipeline);
}

vk::Pipeline CommandBuffer::FlushComputeState(bool synchronous) {
	if (!_pipelineState.Program) { return nullptr; }

	if (!_currentPipeline.Pipeline) { _dirty |= CommandBufferDirtyFlagBits::Pipeline; }

	if (_dirty & (CommandBufferDirtyFlagBits::StaticState | CommandBufferDirtyFlagBits::Pipeline)) {
		vk::Pipeline oldPipeline = _currentPipeline.Pipeline;
		if (!FlushComputePipeline(synchronous)) { return nullptr; }
		if (oldPipeline != _currentPipeline.Pipeline) {
			BindPipeline(vk::PipelineBindPoint::eCompute, _currentPipeline.Pipeline, _currentPipeline.DynamicMask);
		}
	}
	_dirty &= ~(CommandBufferDirtyFlagBits::StaticState | CommandBufferDirtyFlagBits::Pipeline);

	if (!_currentPipeline.Pipeline) { return nullptr; }

	FlushDescriptorSets();

	if (_dirty & CommandBufferDirtyFlagBits::PushConstants) {
		auto& range = _pipelineState.PipelineLayout->GetResourceLayout().PushConstantRange;
		if (range.stageFlags) {
			_commandBuffer.pushConstants(_pipelineLayout, range.stageFlags, 0, range.size, _resources.PushConstantData);
		}
	}
	_dirty &= ~(CommandBufferDirtyFlagBits::PushConstants);

	return _currentPipeline.Pipeline;
}

void CommandBuffer::FlushDescriptorSet(uint32_t set) {
	auto& layout = _pipelineState.PipelineLayout->GetResourceLayout();
	if (layout.BindlessDescriptorSetMask & (1u << set)) {
		/*
		_commandBuffer.bindDescriptorSets(
		  _actualRenderPass ? vk::PipelineBindPoint::eGraphics : vk::PipelineBindPoint::eCompute,
		  _pipelineLayout,
		  set,
		  1,
		  &_bindlessSets[set],
		  0,
		  nullptr);
		*/

		return;
	}

	auto& setLayout                                = layout.SetLayouts[set];
	uint32_t dynamicOffsetCount                    = 0;
	uint32_t dynamicOffsets[MaxDescriptorBindings] = {0};

	Hasher h;
	h(setLayout.FloatMask);

	ForEachBit(setLayout.InputAttachmentMask, [&](uint32_t binding) {
		auto arraySize = setLayout.ArraySizes[binding];
		for (uint32_t i = 0; i < arraySize; ++i) {
			h(_resources.Bindings[set][binding + i].Cookie);
			h(_resources.Bindings[set][binding + i].Image.Float.imageLayout);
		}
	});

	ForEachBit(setLayout.SampledImageMask, [&](uint32_t binding) {
		auto arraySize = setLayout.ArraySizes[binding];
		for (uint32_t i = 0; i < arraySize; ++i) {
			h(_resources.Bindings[set][binding + i].Cookie);
			h(_resources.Bindings[set][binding + i].Image.Float.imageLayout);
		}
	});

	ForEachBit(setLayout.SampledTexelBufferMask, [&](uint32_t binding) {
		auto arraySize = setLayout.ArraySizes[binding];
		for (uint32_t i = 0; i < arraySize; ++i) { h(_resources.Bindings[set][binding + i].Cookie); }
	});

	ForEachBit(setLayout.SamplerMask, [&](uint32_t binding) {
		auto arraySize = setLayout.ArraySizes[binding];
		for (uint32_t i = 0; i < arraySize; ++i) { h(_resources.Bindings[set][binding + i].Cookie); }
	});

	ForEachBit(setLayout.SeparateImageMask, [&](uint32_t binding) {
		auto arraySize = setLayout.ArraySizes[binding];
		for (uint32_t i = 0; i < arraySize; ++i) {
			h(_resources.Bindings[set][binding + i].Cookie);
			h(_resources.Bindings[set][binding + i].Image.Float.imageLayout);
		}
	});

	ForEachBit(setLayout.StorageBufferMask, [&](uint32_t binding) {
		auto arraySize = setLayout.ArraySizes[binding];
		for (uint32_t i = 0; i < arraySize; ++i) {
			h(_resources.Bindings[set][binding + i].Cookie);
			h(_resources.Bindings[set][binding + i].Buffer.offset);
			h(_resources.Bindings[set][binding + i].Buffer.range);
		}
	});

	ForEachBit(setLayout.StorageImageMask, [&](uint32_t binding) {
		auto arraySize = setLayout.ArraySizes[binding];
		for (uint32_t i = 0; i < arraySize; ++i) {
			h(_resources.Bindings[set][binding + i].Cookie);
			h(_resources.Bindings[set][binding + i].Image.Float.imageLayout);
		}
	});

	ForEachBit(setLayout.UniformBufferMask, [&](uint32_t binding) {
		auto arraySize = setLayout.ArraySizes[binding];
		for (uint32_t i = 0; i < arraySize; ++i) {
			h(_resources.Bindings[set][binding + i].Cookie);
			h(_resources.Bindings[set][binding + i].Buffer.range);
			dynamicOffsets[dynamicOffsetCount++] = _resources.Bindings[set][binding + i].DynamicOffset;
		}
	});

	const auto hash = h.Get();
	auto allocated  = _pipelineState.PipelineLayout->GetAllocator(set)->Find(_threadIndex, hash);
	if (!allocated.second) {
		auto updateTemplate = _pipelineState.PipelineLayout->GetUpdateTemplate(set);
		_device.GetDevice().updateDescriptorSetWithTemplate(allocated.first, updateTemplate, _resources.Bindings[set]);
	}

	_commandBuffer.bindDescriptorSets(
		_actualRenderPass ? vk::PipelineBindPoint::eGraphics : vk::PipelineBindPoint::eCompute,
		_pipelineLayout,
		set,
		1,
		&allocated.first,
		dynamicOffsetCount,
		dynamicOffsets);
	_allocatedSets[set] = allocated.first;
}

void CommandBuffer::FlushDescriptorSets() {
	auto& layout = _pipelineState.PipelineLayout->GetResourceLayout();

	uint32_t setUpdate = layout.DescriptorSetMask & _dirtySets;
	ForEachBit(setUpdate, [&](uint32_t set) { FlushDescriptorSet(set); });
	_dirtySets &= ~setUpdate;
	_dirtySetsDynamic &= ~setUpdate;

	uint32_t dynamicSetUpdate = layout.DescriptorSetMask & _dirtySetsDynamic;
	ForEachBit(dynamicSetUpdate, [&](uint32_t set) { RebindDescriptorSet(set); });
	_dirtySetsDynamic &= ~dynamicSetUpdate;
}

void CommandBuffer::RebindDescriptorSet(uint32_t set) {
	auto& layout = _pipelineState.PipelineLayout->GetResourceLayout();
	if (layout.BindlessDescriptorSetMask & (1u << set)) {}

	auto& setLayout             = layout.SetLayouts[set];
	uint32_t dynamicOffsetCount = 0;
	std::array<uint32_t, MaxDescriptorBindings> dynamicOffsets;

	ForEachBit(setLayout.UniformBufferMask, [&](uint32_t binding) {
		const auto arraySize = setLayout.ArraySizes[binding];
		for (uint32_t i = 0; i < arraySize; ++i) {
			dynamicOffsets[dynamicOffsetCount++] = _resources.Bindings[set][binding + i].DynamicOffset;
		}
	});

	_commandBuffer.bindDescriptorSets(
		_actualRenderPass ? vk::PipelineBindPoint::eGraphics : vk::PipelineBindPoint::eCompute,
		_pipelineLayout,
		set,
		1,
		&_allocatedSets[set],
		dynamicOffsetCount,
		dynamicOffsets.data());
}

void CommandBuffer::SetViewportScissor(const RenderPassInfo& rpInfo, const Framebuffer* framebuffer) {
	vk::Rect2D rect     = rpInfo.RenderArea;
	vk::Extent2D extent = framebuffer->GetExtent();

	rect.offset.x      = std::min(int32_t(extent.width), rect.offset.x);
	rect.offset.y      = std::min(int32_t(extent.height), rect.offset.y);
	rect.extent.width  = std::min(extent.width - rect.offset.x, rect.extent.width);
	rect.extent.height = std::min(extent.height - rect.offset.y, rect.extent.height);

	// Note: Viewport is flipped up-side-down here for compatibility with GLM matrices.
	_viewport = vk::Viewport(
		float(rect.offset.x), float(rect.extent.height), float(rect.extent.width), -float(rect.extent.height), 0.0f, 1.0f);
	_scissor = rect;
}
}  // namespace Vulkan
}  // namespace Luna
