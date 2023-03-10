#include <Luna/Renderer/RenderGraph.hpp>
#include <Luna/Renderer/RenderPass.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/Semaphore.hpp>
#include <glm/glm.hpp>

namespace Luna {
static constexpr RenderGraphQueueFlags ComputeQueues =
	RenderGraphQueueFlagBits::Compute | RenderGraphQueueFlagBits::AsyncCompute;

RenderGraph::RenderGraph(Vulkan::Device& device) : _device(device) {}

RenderGraph::~RenderGraph() noexcept {}

void RenderGraph::Bake() {
	// Allow the Render Passes a chance to set up their dependencies.
	for (auto& pass : _passes) { pass->SetupDependencies(); }

	// Ensure that the Render Graph is sane.
	ValidatePasses();

	// Clean up any information we created last time this graph was baked.
	_passStack.clear();
	_passDependencies.clear();
	_passMergeDependencies.clear();
	_passDependencies.resize(_passes.size());
	_passMergeDependencies.resize(_passes.size());

	// Ensure our backbuffer source exists, and has a Render Pass which writes to it.
	auto it = _resourceToIndex.find(_backbufferSource);
	if (it == _resourceToIndex.end()) { throw std::logic_error("[RenderGraph] Backbuffer source does not exist."); }
	auto& backbufferResource = *_resources[it->second];
	if (backbufferResource.GetWritePasses().empty()) {
		throw std::logic_error("[RenderGraph] Backbuffer source is never written to.");
	}

	// Start our graph by adding all of the backbuffer source's dependencies to the stack.
	for (auto& pass : backbufferResource.GetWritePasses()) { _passStack.push_back(pass); }

	// Traverse the Render Pass stack and each each Render Pass's dependencies along the way.
	auto tmpPassStack = _passStack;  // Need a copy since the original will be modified during iteration.
	for (auto& pushedPass : tmpPassStack) { TraverseDependencies(*_passes[pushedPass], 0); }

	// We now have a stack with the final output at the top, so we need to reverse it.
	std::reverse(_passStack.begin(), _passStack.end());

	// Ensure each Render Pass only appears in the stack once.
	FilterPasses();

	// Reorder the passes so that we're running as many things in parallel as possible.
	ReorderPasses();

	// We now have a complete, linear list of render passes which obey dependencies.

	// Determine what physical resources we need. This includes simple aliasing, using the same physical attachment where
	// possible. e.g. Depth Input -> Depth Output
	BuildPhysicalResources();

	// Build our physical passes, which may contain more than one RenderPass if it is possible to merge them together.
	BuildPhysicalPasses();

	// After merging everything we can, if an image is only used in one physical pass, make it transient.
	BuildTransients();

	// Now we can build our actual render pass info.
	BuildRenderPassInfo();

	// Determine the barriers needed for each Render Pass in isolation.
	BuildBarriers();

	_swapchainPhysicalIndex = backbufferResource.GetPhysicalIndex();
	auto& backbufferDim     = _physicalDimensions[_swapchainPhysicalIndex];
	bool canAliasBackbuffer =
		(backbufferDim.Queues & ComputeQueues) == 0 && backbufferDim.Flags & AttachmentInfoFlagBits::InternalTransient;
	for (auto& dim : _physicalDimensions) {
		if (&dim != &backbufferDim) { dim.Transform = vk::SurfaceTransformFlagBitsKHR::eIdentity; }
	}
	backbufferDim.Flags &= ~(AttachmentInfoFlagBits::InternalTransient | AttachmentInfoFlagBits::SupportsPrerotate);
	backbufferDim.Flags |= _swapchainDimensions.Flags & AttachmentInfoFlagBits::Persistent;
	if (!canAliasBackbuffer || backbufferDim != _swapchainDimensions) {
		_swapchainPhysicalIndex = RenderResource::Unused;
		if (!(backbufferDim.Queues & RenderGraphQueueFlagBits::Graphics)) {
			backbufferDim.Queues |= RenderGraphQueueFlagBits::AsyncGraphics;
		} else {
			backbufferDim.Queues |= RenderGraphQueueFlagBits::Graphics;
		}

		backbufferDim.ImageUsage |= vk::ImageUsageFlagBits::eSampled;
		backbufferDim.Transform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
	} else {
		_physicalDimensions[_swapchainPhysicalIndex].Flags |= AttachmentInfoFlagBits::InternalTransient;
	}

	BuildPhysicalBarriers();
	BuildAliases();

	for (auto& physicalPass : _physicalPasses) {
		for (auto pass : physicalPass.Passes) { _passes[pass]->Setup(_device); }
	}
}

void RenderGraph::EnqueueRenderPasses(Vulkan::Device& device, TaskComposer& composer) {
	auto* threading = Threading::Get();

	size_t count = _physicalPasses.size();
	_passSubmissionStates.clear();
	_passSubmissionStates.resize(count);

	for (size_t i = 0; i < count; ++i) {
		EnqueueRenderPass(device, _physicalPasses[i], _passSubmissionStates[i], composer);
	}
	for (size_t i = 0; i < count; ++i) {
		if (_passSubmissionStates[i].Active) {
			PhysicalPassHandleGPU(device, _physicalPasses[i], _passSubmissionStates[i]);
		}
	}

	for (auto& state : _passSubmissionStates) {
		auto& group = composer.BeginPipelineStage();
		if (state.RenderingDependency) {
			threading->AddDependency(group, *state.RenderingDependency);
			state.RenderingDependency.Reset();
		}

		group.Enqueue([&state]() { state.Submit(); });
	}

	if (_swapchainPhysicalIndex == RenderResource::Unused) {
		auto& group = composer.BeginPipelineStage();
		group.Enqueue([this, &device]() { device.FlushFrame(); });
	} else {
		auto& group = composer.BeginPipelineStage();
		group.Enqueue([this, &device]() { device.FlushFrame(); });
	}
}

void RenderGraph::Log() {
	Log::Debug("RenderGraph", "===== Baked Render Graph Information =====");

	Log::Debug("RenderGraph", "Resources ({}):", _physicalDimensions.size());
	for (uint32_t i = 0; i < _physicalDimensions.size(); ++i) {
		const auto& resource = _physicalDimensions[i];

		if (resource.BufferInfo.Size) {
			Log::Debug("RenderGraph", "- Buffer #{} ({}):", i, resource.Name);
			Log::Debug("RenderGraph", "  - Size: {}", resource.BufferInfo.Size);
			Log::Debug("RenderGraph", "  - Usage: {}", vk::to_string(resource.BufferInfo.Usage));
		} else {
			Log::Debug(
				"RenderGraph", "- Texture #{} ({}):{}", i, resource.Name, i == _swapchainPhysicalIndex ? " (Swapchain)" : "");
			Log::Debug("RenderGraph", "  - Format: {}", vk::to_string(resource.Format));
			Log::Debug("RenderGraph", "  - Extent: {}x{}x{}", resource.Width, resource.Height, resource.Depth);
			Log::Debug(
				"RenderGraph", "  - Layers: {}, Levels: {}, Samples: {}", resource.Layers, resource.Levels, resource.Samples);
			Log::Debug("RenderGraph", "  - Usage: {}", vk::to_string(resource.ImageUsage));
			Log::Debug(
				"RenderGraph", "  - Transient: {}", resource.Flags & AttachmentInfoFlagBits::InternalTransient ? "Yes" : "No");
		}
	}

	const auto Resource = [&](uint32_t physicalIndex) {
		const auto& dim = _physicalDimensions[physicalIndex];
		return fmt::format("{} ({})", physicalIndex, dim.Name);
	};

	Log::Debug("RenderGraph", "Physical Passes ({}):", _physicalPasses.size());
	for (uint32_t i = 0; i < _physicalPasses.size(); ++i) {
		const auto& physicalPass = _physicalPasses[i];

		Log::Debug("RenderGraph", "- Physical Pass #{}:", i);

		for (auto& barrier : physicalPass.Invalidate) {
			Log::Debug("RenderGraph",
			           "  - Invalidate: {}, Layout: {}, Access: {}, Stages: {}",
			           Resource(barrier.ResourceIndex),
			           vk::to_string(barrier.Layout),
			           vk::to_string(barrier.Access),
			           vk::to_string(barrier.Stages));
		}

		for (uint32_t j = 0; j < physicalPass.Passes.size(); ++j) {
			const auto& pass = *_passes[physicalPass.Passes[j]];

			Log::Debug("RenderGraph", "  - Render Pass #{} ({}):", j, pass.GetName());

			const auto& barriers = _passBarriers[physicalPass.Passes[j]];
			for (auto& barrier : barriers.Invalidate) {
				if (!(_physicalDimensions[barrier.ResourceIndex].Flags & AttachmentInfoFlagBits::InternalTransient)) {
					Log::Debug("RenderGraph",
					           "    - Invalidate: {}, Layout: {}, Access: {}, Stages: {}",
					           Resource(barrier.ResourceIndex),
					           vk::to_string(barrier.Layout),
					           vk::to_string(barrier.Access),
					           vk::to_string(barrier.Stages));
				}
			}

			if (pass.GetDepthStencilOutput()) {
				Log::Debug(
					"RenderGraph", "    - Depth/Stencil R/W: {}", Resource(pass.GetDepthStencilOutput()->GetPhysicalIndex()));
			} else if (pass.GetDepthStencilInput()) {
				Log::Debug(
					"RenderGraph", "    - Depth/Stencil Read: {}", Resource(pass.GetDepthStencilInput()->GetPhysicalIndex()));
			}

			const auto Attachments = [&](const std::string& type, const std::vector<RenderTextureResource*>& attachments) {
				for (size_t att = 0; att < attachments.size(); ++att) {
					Log::Debug("RenderGraph", "    - {} #{}: {}", type, att, Resource(attachments[att]->GetPhysicalIndex()));
				}
			};
			Attachments("Color", pass.GetColorOutputs());
			Attachments("Resolve", pass.GetResolveOutputs());
			Attachments("Input", pass.GetAttachmentInputs());
			for (size_t att = 0; att < pass.GetGenericTextureInputs().size(); ++att) {
				Log::Debug("RenderGraph",
				           "    - Texture #{}: {}",
				           att,
				           Resource(pass.GetGenericTextureInputs()[att].Texture->GetPhysicalIndex()));
			}
			for (size_t att = 0; att < pass.GetGenericBufferInputs().size(); ++att) {
				Log::Debug("RenderGraph",
				           "    - Buffer #{}: {}",
				           att,
				           Resource(pass.GetGenericBufferInputs()[att].Buffer->GetPhysicalIndex()));
			}

			for (auto& barrier : barriers.Flush) {
				if (!(_physicalDimensions[barrier.ResourceIndex].Flags & AttachmentInfoFlagBits::InternalTransient) &&
				    barrier.ResourceIndex != _swapchainPhysicalIndex) {
					Log::Debug("RenderGraph",
					           "    - Flush: {}, Layout: {}, Access: {}, Stages: {}",
					           Resource(barrier.ResourceIndex),
					           vk::to_string(barrier.Layout),
					           vk::to_string(barrier.Access),
					           vk::to_string(barrier.Stages));
				}
			}
		}

		for (auto& barrier : physicalPass.Flush) {
			Log::Debug("RenderGraph",
			           "  - Flush: {}, Layout: {}, Access: {}, Stages: {}",
			           Resource(barrier.ResourceIndex),
			           vk::to_string(barrier.Layout),
			           vk::to_string(barrier.Access),
			           vk::to_string(barrier.Stages));
		}
	}
}

void RenderGraph::Reset() {
	_passes.clear();
	_resources.clear();
	_passToIndex.clear();
	_resourceToIndex.clear();
	_physicalPasses.clear();
	_physicalDimensions.clear();
	_physicalAttachments.clear();
	_physicalBuffers.clear();
	_physicalImageAttachments.clear();
	_physicalEvents.clear();
	_physicalHistoryEvents.clear();
	_physicalHistoryImageAttachments.clear();
}

void RenderGraph::SetupAttachments(Vulkan::ImageView* swapchain) {
	_physicalAttachments.clear();
	_physicalAttachments.resize(_physicalDimensions.size());
	_physicalBuffers.resize(_physicalDimensions.size());
	_physicalImageAttachments.resize(_physicalDimensions.size());
	_physicalHistoryImageAttachments.resize(_physicalDimensions.size());
	_physicalEvents.resize(_physicalDimensions.size());
	_physicalHistoryEvents.resize(_physicalDimensions.size());

	_swapchainAttachment = swapchain;

	uint32_t attachmentCount = _physicalDimensions.size();
	for (uint32_t i = 0; i < attachmentCount; ++i) {
		if (_physicalImageHasHistory[i]) {
			std::swap(_physicalHistoryImageAttachments[i], _physicalImageAttachments[i]);
			std::swap(_physicalHistoryEvents[i], _physicalEvents[i]);
		}

		auto& att = _physicalDimensions[i];
		if (att.Flags & AttachmentInfoFlagBits::InternalProxy) { continue; }

		if (att.BufferInfo.Size != 0) {
		} else {
			if (att.IsStorageImage()) {
			} else if (i == _swapchainPhysicalIndex) {
				_physicalAttachments[i] = swapchain;
			} else if (att.Flags & AttachmentInfoFlagBits::InternalTransient) {
				_physicalImageAttachments[i] = _device.GetTransientAttachment(
					vk::Extent2D(att.Width, att.Height), att.Format, i, vk::SampleCountFlagBits::e1, att.Layers);
				_physicalAttachments[i] = &_physicalImageAttachments[i]->GetView();
			} else {
			}
		}
	}

	for (auto& physicalPass : _physicalPasses) {
		uint32_t layers = ~0u;

		uint32_t colorAttachmentCount = physicalPass.PhysicalColorAttachments.size();
		for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
			auto& att = physicalPass.RenderPassInfo.ColorAttachments[i];
			att       = _physicalAttachments[physicalPass.PhysicalColorAttachments[i]];

			if (att->GetImage()->GetCreateInfo().Domain == Vulkan::ImageDomain::Physical) {
				layers = std::min(layers, att->GetImage()->GetCreateInfo().ArrayLayers);
			}
		}

		if (physicalPass.PhysicalDepthStencilAttachment != RenderResource::Unused) {
			auto& ds = physicalPass.RenderPassInfo.DepthStencilAttachment;
			ds       = _physicalAttachments[physicalPass.PhysicalDepthStencilAttachment];

			if (ds->GetImage()->GetCreateInfo().Domain == Vulkan::ImageDomain::Physical) {
				layers = std::min(layers, ds->GetImage()->GetCreateInfo().ArrayLayers);
			}
		} else {
			physicalPass.RenderPassInfo.DepthStencilAttachment = nullptr;
		}

		physicalPass.Layers = layers;
	}
}

RenderPass& RenderGraph::AddPass(const std::string& name, RenderGraphQueueFlagBits queue) {
	auto it = _passToIndex.find(name);
	if (it != _passToIndex.end()) { return *_passes[it->second]; }

	uint32_t index = _passes.size();
	_passes.emplace_back(new RenderPass(*this, index, queue));
	_passes.back()->SetName(name);
	_passToIndex[name] = index;

	return *_passes.back();
}

RenderPass* RenderGraph::FindPass(const std::string& name) {
	auto it = _passToIndex.find(name);
	if (it != _passToIndex.end()) { return _passes[it->second].get(); }

	return nullptr;
}

std::vector<Vulkan::BufferHandle> RenderGraph::ConsumePhysicalBuffers() const {
	return _physicalBuffers;
}

RenderBufferResource& RenderGraph::GetBufferResource(const std::string& name) {
	auto it = _resourceToIndex.find(name);
	if (it != _resourceToIndex.end()) {
		assert(_resources[it->second]->GetType() == RenderResource::Type::Buffer);

		return static_cast<RenderBufferResource&>(*_resources[it->second]);
	}

	uint32_t index = _resources.size();
	_resources.emplace_back(new RenderBufferResource(index));
	_resources.back()->SetName(name);
	_resourceToIndex[name] = index;

	return static_cast<RenderBufferResource&>(*_resources.back());
}

RenderResource& RenderGraph::GetProxyResource(const std::string& name) {
	auto it = _resourceToIndex.find(name);
	if (it != _resourceToIndex.end()) {
		assert(_resources[it->second]->GetType() == RenderResource::Type::Proxy);

		return *_resources[it->second];
	}

	uint32_t index = _resources.size();
	_resources.emplace_back(new RenderResource(RenderResource::Type::Proxy, index));
	_resources.back()->SetName(name);
	_resourceToIndex[name] = index;

	return *_resources.back();
}

ResourceDimensions RenderGraph::GetResourceDimensions(const RenderBufferResource& resource) const {
	ResourceDimensions dim = {};
	auto& info             = resource.GetBufferInfo();
	dim.BufferInfo         = info;
	dim.BufferInfo.Usage |= resource.GetBufferUsage();
	dim.Flags |= info.Flags;
	dim.Name = resource.GetName();

	return dim;
}

ResourceDimensions RenderGraph::GetResourceDimensions(const RenderTextureResource& resource) const {
	ResourceDimensions dim = {};
	auto& info             = resource.GetAttachmentInfo();
	dim.Flags  = info.Flags & ~(AttachmentInfoFlagBits::SupportsPrerotate | AttachmentInfoFlagBits::InternalTransient);
	dim.Format = info.Format;
	dim.ImageUsage = info.AuxUsage | resource.GetImageUsage();
	dim.Layers     = info.Layers;
	dim.Name       = resource.GetName();
	dim.Queues     = resource.GetUsedQueues();
	dim.Samples    = info.Samples;

	if (info.Flags & AttachmentInfoFlagBits::SupportsPrerotate) { dim.Transform = _swapchainDimensions.Transform; }
	if (dim.Format == vk::Format::eUndefined) { dim.Format = _swapchainDimensions.Format; }
	if (resource.GetTransientState()) { dim.Flags |= AttachmentInfoFlagBits::InternalTransient; }

	switch (info.SizeClass) {
		case SizeClass::SwapchainRelative:
			dim.Width  = std::max(uint32_t(glm::ceil(info.Width * _swapchainDimensions.Width)), 1u);
			dim.Height = std::max(uint32_t(glm::ceil(info.Height * _swapchainDimensions.Height)), 1u);
			dim.Depth  = std::max(uint32_t(glm::ceil(info.Depth)), 1u);
			break;

		case SizeClass::Absolute:
			dim.Width  = std::max(uint32_t(info.Width), 1u);
			dim.Height = std::max(uint32_t(info.Height), 1u);
			dim.Depth  = std::max(uint32_t(info.Depth), 1u);
			break;

		case SizeClass::InputRelative:
			auto it = _resourceToIndex.find(info.SizeRelativeName);
			if (it == _resourceToIndex.end()) {
				throw std::logic_error("[RenderGraph] Input Relative resource does not exist!");
			}
			const auto& input = static_cast<const RenderTextureResource&>(*_resources[it->second]);
			auto inputDim     = GetResourceDimensions(input);

			dim.Width  = std::max(uint32_t(glm::ceil(info.Width * inputDim.Width)), 1u);
			dim.Height = std::max(uint32_t(glm::ceil(info.Height * inputDim.Height)), 1u);
			dim.Depth  = std::max(uint32_t(glm::ceil(info.Depth * inputDim.Depth)), 1u);
			break;
	}

	const auto mipLevels = Vulkan::CalculateMipLevels(dim.Width, dim.Height, dim.Depth);
	dim.Levels           = std::min(mipLevels, info.Levels == 0 ? ~0u : info.Levels);

	return dim;
}

RenderTextureResource& RenderGraph::GetTextureResource(const std::string& name) {
	auto it = _resourceToIndex.find(name);
	if (it != _resourceToIndex.end()) {
		assert(_resources[it->second]->GetType() == RenderResource::Type::Texture);

		return static_cast<RenderTextureResource&>(*_resources[it->second]);
	}

	uint32_t index = _resources.size();
	_resources.emplace_back(new RenderTextureResource(index));
	_resources.back()->SetName(name);
	_resourceToIndex[name] = index;

	return static_cast<RenderTextureResource&>(*_resources.back());
}

void RenderGraph::InstallPhysicalBuffers(std::vector<Vulkan::BufferHandle>& buffers) {
	_physicalBuffers = std::move(buffers);
}

void RenderGraph::SetBackbufferDimensions(const ResourceDimensions& dim) {
	_swapchainDimensions = dim;
}

void RenderGraph::SetBackbufferSource(const std::string& name) {
	_backbufferSource = name;
}

void RenderGraph::BuildAliases() {}

void RenderGraph::BuildBarriers() {
	_passBarriers.clear();
	_passBarriers.reserve(_passStack.size());

	// Get a barrier for the given attachment, or create one if none exists yet.
	auto GetAccess = [](std::vector<Barrier>& barriers, uint32_t index, bool history) -> Barrier& {
		auto it = std::find_if(barriers.begin(), barriers.end(), [index, history](const Barrier& b) {
			return index == b.ResourceIndex && history == b.History;
		});
		if (it != barriers.end()) { return *it; }

		barriers.push_back(Barrier{index, vk::ImageLayout::eUndefined, {}, {}, history});
		return barriers.back();
	};

	for (auto& index : _passStack) {
		auto& pass        = *_passes[index];
		Barriers barriers = {};

		auto GetFlushAccess      = [&](uint32_t i) -> Barrier& { return GetAccess(barriers.Flush, i, false); };
		auto GetInvalidateAccess = [&](uint32_t i, bool history) -> Barrier& {
			return GetAccess(barriers.Invalidate, i, history);
		};

		// Handle all of our inputs (invalidate barriers).
		for (auto& input : pass.GetGenericBufferInputs()) {
			auto& barrier = GetInvalidateAccess(input.Buffer->GetPhysicalIndex(), false);
			barrier.Access |= input.Access;
			barrier.Stages |= input.Stages;
			if (barrier.Layout != vk::ImageLayout::eUndefined) { throw std::logic_error("[RenderGraph] Layout mismatch."); }
			barrier.Layout = input.Layout;
		}

		// Handle all of our outputs (flush barriers).
		for (auto* output : pass.GetColorOutputs()) {
			if (pass.GetQueue() & ComputeQueues) {
				throw std::logic_error("[RenderGraph] Only graphics passes can have color outputs.");
			}

			auto& barrier = GetFlushAccess(output->GetPhysicalIndex());
			if ((_physicalDimensions[output->GetPhysicalIndex()].Levels > 1) &&
			    _physicalDimensions[output->GetPhysicalIndex()].Flags & AttachmentInfoFlagBits::GenerateMips) {
				barrier.Access |= vk::AccessFlagBits2::eTransferRead;
				barrier.Stages |= vk::PipelineStageFlagBits2::eBlit;
				if (barrier.Layout != vk::ImageLayout::eUndefined) { throw std::logic_error("[RenderGraph] Layout mismatch."); }
				barrier.Layout = vk::ImageLayout::eTransferSrcOptimal;
			} else {
				barrier.Access |= vk::AccessFlagBits2::eColorAttachmentWrite;
				barrier.Stages |= vk::PipelineStageFlagBits2::eColorAttachmentOutput;

				if (barrier.Layout == vk::ImageLayout::eShaderReadOnlyOptimal || barrier.Layout == vk::ImageLayout::eGeneral) {
					barrier.Layout = vk::ImageLayout::eGeneral;
				} else if (barrier.Layout != vk::ImageLayout::eUndefined) {
					throw std::logic_error("[RenderGraph] Layout mismatch.");
				} else {
					barrier.Layout = vk::ImageLayout::eColorAttachmentOptimal;
				}
			}
		}

		// Finally, handle depth/stencil, which can be invalidate, or flush, or both.
		auto* dsInput  = pass.GetDepthStencilInput();
		auto* dsOutput = pass.GetDepthStencilOutput();
		if ((dsInput || dsOutput) && (pass.GetQueue() & ComputeQueues)) {
			throw std::logic_error("[RenderGraph] Only graphics passes can have depth attachments.");
		}
		if (dsInput && dsOutput) {
			auto& dstBarrier = GetInvalidateAccess(dsInput->GetPhysicalIndex(), false);
			auto& srcBarrier = GetFlushAccess(dsOutput->GetPhysicalIndex());

			if (dstBarrier.Layout == vk::ImageLayout::eShaderReadOnlyOptimal) {
				dstBarrier.Layout = vk::ImageLayout::eGeneral;
			} else if (dstBarrier.Layout != vk::ImageLayout::eUndefined) {
				throw std::logic_error("[RenderGraph] Layout mismatch.");
			} else {
				dstBarrier.Layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			}

			dstBarrier.Access |=
				vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
			dstBarrier.Stages |=
				vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;

			srcBarrier.Layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			srcBarrier.Access |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
			srcBarrier.Stages |= vk::PipelineStageFlagBits2::eLateFragmentTests;
		} else if (dsInput) {
			auto& dstBarrier = GetInvalidateAccess(dsInput->GetPhysicalIndex(), false);

			if (dstBarrier.Layout == vk::ImageLayout::eShaderReadOnlyOptimal) {
				dstBarrier.Layout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
			} else if (dstBarrier.Layout != vk::ImageLayout::eUndefined) {
				throw std::logic_error("[RenderGraph] Layout mismatch.");
			} else {
				dstBarrier.Layout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
			}

			dstBarrier.Access |= vk::AccessFlagBits2::eDepthStencilAttachmentRead;
			dstBarrier.Stages |=
				vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
		} else if (dsOutput) {
			auto& srcBarrier = GetFlushAccess(dsOutput->GetPhysicalIndex());

			if (srcBarrier.Layout == vk::ImageLayout::eShaderReadOnlyOptimal) {
				srcBarrier.Layout = vk::ImageLayout::eGeneral;
			} else if (srcBarrier.Layout != vk::ImageLayout::eUndefined) {
				throw std::logic_error("[RenderGraph] Layout mismatch.");
			} else {
				srcBarrier.Layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			}

			srcBarrier.Access |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
			srcBarrier.Stages |= vk::PipelineStageFlagBits2::eLateFragmentTests;
		}

		_passBarriers.push_back(std::move(barriers));
	}
}

void RenderGraph::BuildPhysicalBarriers() {}

void RenderGraph::BuildPhysicalPasses() {
	_physicalPasses.clear();

	PhysicalPass physicalPass;

	/**
	 * Determine whether the given resource exists within the resource list, by comparing the assigned physical index.
	 */
	const auto FindAttachment = [](const std::vector<RenderTextureResource*>& resourceList,
	                               const RenderTextureResource* resource) -> bool {
		if (!resource) { return false; }

		auto it = std::find_if(resourceList.begin(), resourceList.end(), [resource](const RenderTextureResource* res) {
			return res->GetPhysicalIndex() == resource->GetPhysicalIndex();
		});

		return it != resourceList.end();
	};
	const auto FindBuffer = [](const std::vector<RenderBufferResource*>& resourceList,
	                           const RenderBufferResource* resource) -> bool {
		if (!resource) { return false; }

		auto it = std::find_if(resourceList.begin(), resourceList.end(), [resource](const RenderBufferResource* res) {
			return res->GetPhysicalIndex() == resource->GetPhysicalIndex();
		});

		return it != resourceList.end();
	};

	/**
	 * Determine whether we should merge the two given Render Passes into a single Physical Pass.
	 */
	const auto ShouldMerge = [&](const RenderPass& prev, const RenderPass& next) -> bool {
		// We can only merge render passes which are Graphics, and within the same Queue.
		if ((prev.GetQueue() & ComputeQueues) || (prev.GetQueue() != prev.GetQueue())) { return false; }

		// If we need to generate mipmaps after this pass, we cannot merge.
		for (auto* output : prev.GetColorOutputs()) {
			if ((_physicalDimensions[output->GetPhysicalIndex()].Levels > 1) &&
			    _physicalDimensions[output->GetPhysicalIndex()].Flags & AttachmentInfoFlagBits::GenerateMips) {
				return false;
			}
		}

		// If the previous render pass writes to an output that we need to use as an input, we cannot merge.
		for (auto& input : next.GetGenericTextureInputs()) {
			if (FindAttachment(prev.GetColorOutputs(), input.Texture)) { return false; }
			if (FindAttachment(prev.GetResolveOutputs(), input.Texture)) { return false; }
			if (FindAttachment(prev.GetStorageTextureOutputs(), input.Texture)) { return false; }
			if (FindAttachment(prev.GetBlitTextureOutputs(), input.Texture)) { return false; }
			if (input.Texture && prev.GetDepthStencilOutput() == input.Texture) { return false; }
		}
		for (auto& input : next.GetGenericBufferInputs()) {
			if (FindBuffer(prev.GetStorageOutputs(), input.Buffer)) { return false; }
		}
		for (auto* input : next.GetBlitTextureInputs()) {
			if (FindAttachment(prev.GetBlitTextureOutputs(), input)) { return false; }
		}
		for (auto* input : next.GetColorInputs()) {
			if (!input) { continue; }
			if (FindAttachment(prev.GetStorageTextureOutputs(), input)) { return false; }
			if (FindAttachment(prev.GetBlitTextureOutputs(), input)) { return false; }
		}
		for (auto* input : next.GetColorScaleInputs()) {
			if (FindAttachment(prev.GetStorageTextureOutputs(), input)) { return false; }
			if (FindAttachment(prev.GetBlitTextureOutputs(), input)) { return false; }
			if (FindAttachment(prev.GetColorOutputs(), input)) { return false; }
			if (FindAttachment(prev.GetResolveOutputs(), input)) { return false; }
		}
		for (auto* input : next.GetStorageInputs()) {
			if (FindBuffer(prev.GetStorageOutputs(), input)) { return false; }
		}
		for (auto* input : next.GetStorageTextureInputs()) {
			if (FindAttachment(prev.GetStorageTextureOutputs(), input)) { return false; }
		}

		// Helper functions to determine if two resources are the same or different physical resources.
		const auto DifferentAttachment = [](const RenderResource* a, const RenderResource* b) -> bool {
			return a && b && a->GetPhysicalIndex() != b->GetPhysicalIndex();
		};
		const auto SameAttachment = [](const RenderResource* a, const RenderResource* b) -> bool {
			return a && b && a->GetPhysicalIndex() == b->GetPhysicalIndex();
		};

		// If any of the depth attachments differ, we cannot merge.
		if (DifferentAttachment(next.GetDepthStencilInput(), prev.GetDepthStencilInput())) { return false; }
		if (DifferentAttachment(next.GetDepthStencilInput(), prev.GetDepthStencilOutput())) { return false; }
		if (DifferentAttachment(next.GetDepthStencilOutput(), prev.GetDepthStencilInput())) { return false; }
		if (DifferentAttachment(next.GetDepthStencilOutput(), prev.GetDepthStencilOutput())) { return false; }

		// We have determined all of the reasons why we cannot merge, now we try and determine if we should merge.

		// If the previous render pass write to a color or resolve output that we use as color input, it's the perfect time
		// for a subpass.
		for (auto* input : next.GetColorInputs()) {
			if (!input) { continue; }
			if (FindAttachment(prev.GetColorOutputs(), input)) { return true; }
			if (FindAttachment(prev.GetResolveOutputs(), input)) { return true; }
		}

		// If the depth/stencil attachments are the same for both passes, we can run them simultaneously.
		if (SameAttachment(next.GetDepthStencilInput(), prev.GetDepthStencilInput()) ||
		    SameAttachment(next.GetDepthStencilInput(), prev.GetDepthStencilOutput())) {
			return true;
		}

		// If the previous render pass write to a color, resolve, or depth/stencil output that we use as an attachment
		// input, it's the perfect time for a subpass.
		for (auto* input : next.GetAttachmentInputs()) {
			if (FindAttachment(prev.GetColorOutputs(), input)) { return true; }
			if (FindAttachment(prev.GetResolveOutputs(), input)) { return true; }
			if (input && prev.GetDepthStencilOutput() == input) { return true; }
		}

		// If we reach this point, we have determined that we are technically able to merge, but we have no good reason to
		// do so, so we won't.
		return false;
	};

	// Try and merge as many passes together as you can.
	for (uint32_t index = 0; index < _passStack.size();) {
		uint32_t mergeEnd = index + 1;
		for (; mergeEnd < _passStack.size(); mergeEnd++) {
			bool merge = true;
			for (uint32_t mergeStart = index; mergeStart < mergeEnd; ++mergeStart) {
				if (!ShouldMerge(*_passes[_passStack[mergeStart]], *_passes[_passStack[mergeEnd]])) {
					merge = false;
					break;
				}
			}

			if (!merge) { break; }
		}

		// Add each of the merged passes to the physical pass.
		physicalPass.Passes.insert(physicalPass.Passes.end(), _passStack.begin() + index, _passStack.begin() + mergeEnd);
		_physicalPasses.push_back(std::move(physicalPass));
		index = mergeEnd;
	}

	// Set the physical pass index for each of our render passes.
	for (uint32_t i = 0; i < _physicalPasses.size(); ++i) {
		auto& physicalPass = _physicalPasses[i];
		for (auto& pass : physicalPass.Passes) { _passes[pass]->SetPhysicalPassIndex(i); }
	}
}

void RenderGraph::BuildPhysicalResources() {
	uint32_t physicalIndex = 0;  // The next index to assign to a physical resource.

	for (const auto& passIndex : _passStack) {
		auto& pass = *_passes[passIndex];

		// Here, we go through each of the input resources used for a Render Pass and assign them a physical index.
		// If a resource already has a physical index, we add to its queue flags and usage information.
		//
		// If possible, we try to use 1 physical resource for both input and output, since there's no point in having them
		// be different.

		// Handle the generic inputs first.
		for (auto& input : pass.GetGenericBufferInputs()) {
			if (input.Buffer->GetPhysicalIndex() == RenderResource::Unused) {
				_physicalDimensions.push_back(GetResourceDimensions(*input.Buffer));
				input.Buffer->SetPhysicalIndex(physicalIndex++);
			} else {
				_physicalDimensions[input.Buffer->GetPhysicalIndex()].Queues |= input.Buffer->GetUsedQueues();
				_physicalDimensions[input.Buffer->GetPhysicalIndex()].BufferInfo.Usage |= input.Buffer->GetBufferUsage();
			}
		}

		for (auto& input : pass.GetGenericTextureInputs()) {
			if (input.Texture->GetPhysicalIndex() == RenderResource::Unused) {
				_physicalDimensions.push_back(GetResourceDimensions(*input.Texture));
				input.Texture->SetPhysicalIndex(physicalIndex++);
			} else {
				_physicalDimensions[input.Texture->GetPhysicalIndex()].Queues |= input.Texture->GetUsedQueues();
				_physicalDimensions[input.Texture->GetPhysicalIndex()].ImageUsage |= input.Texture->GetImageUsage();
			}
		}

		// Handle color scaling before other color inputs.
		for (auto* input : pass.GetColorScaleInputs()) {
			if (input) {
				if (input->GetPhysicalIndex() == RenderResource::Unused) {
					_physicalDimensions.push_back(GetResourceDimensions(*input));
					input->SetPhysicalIndex(physicalIndex++);
					_physicalDimensions[input->GetPhysicalIndex()].ImageUsage |= vk::ImageUsageFlagBits::eSampled;
				} else {
					_physicalDimensions[input->GetPhysicalIndex()].Queues |= input->GetUsedQueues();
					_physicalDimensions[input->GetPhysicalIndex()].ImageUsage |=
						input->GetImageUsage() | vk::ImageUsageFlagBits::eSampled;
				}
			}
		}

		// Handle the resources which may be able to be aliased.
		if (!pass.GetColorInputs().empty()) {
			uint32_t count = pass.GetColorInputs().size();
			for (uint32_t i = 0; i < count; ++i) {
				auto* input = pass.GetColorInputs()[i];
				if (input) {
					if (input->GetPhysicalIndex() == RenderResource::Unused) {
						_physicalDimensions.push_back(GetResourceDimensions(*input));
						input->SetPhysicalIndex(physicalIndex++);
					} else {
						_physicalDimensions[input->GetPhysicalIndex()].Queues |= input->GetUsedQueues();
						_physicalDimensions[input->GetPhysicalIndex()].ImageUsage |= input->GetImageUsage();
					}

					if (pass.GetColorOutputs()[i]->GetPhysicalIndex() == RenderResource::Unused) {
						pass.GetColorOutputs()[i]->SetPhysicalIndex(input->GetPhysicalIndex());
					} else if (pass.GetColorOutputs()[i]->GetPhysicalIndex() != input->GetPhysicalIndex()) {
						throw std::logic_error("[RenderGraph] Cannot alias resources, index already claimed.");
					}
				}
			}
		}

		if (!pass.GetStorageInputs().empty()) {
			uint32_t count = pass.GetStorageInputs().size();
			for (uint32_t i = 0; i < count; ++i) {
				auto* input = pass.GetStorageInputs()[i];
				if (input) {
					if (input->GetPhysicalIndex() == RenderResource::Unused) {
						_physicalDimensions.push_back(GetResourceDimensions(*input));
						input->SetPhysicalIndex(physicalIndex++);
					} else {
						_physicalDimensions[input->GetPhysicalIndex()].Queues |= input->GetUsedQueues();
						_physicalDimensions[input->GetPhysicalIndex()].BufferInfo.Usage |= input->GetBufferUsage();
					}

					if (pass.GetStorageOutputs()[i]->GetPhysicalIndex() == RenderResource::Unused) {
						pass.GetStorageOutputs()[i]->SetPhysicalIndex(input->GetPhysicalIndex());
					} else if (pass.GetStorageOutputs()[i]->GetPhysicalIndex() != input->GetPhysicalIndex()) {
						throw std::logic_error("[RenderGraph] Cannot alias resources, index already claimed.");
					}
				}
			}
		}

		if (!pass.GetBlitTextureInputs().empty()) {
			uint32_t count = pass.GetBlitTextureInputs().size();
			for (uint32_t i = 0; i < count; ++i) {
				auto* input = pass.GetBlitTextureInputs()[i];
				if (input) {
					if (input->GetPhysicalIndex() == RenderResource::Unused) {
						_physicalDimensions.push_back(GetResourceDimensions(*input));
						input->SetPhysicalIndex(physicalIndex++);
					} else {
						_physicalDimensions[input->GetPhysicalIndex()].Queues |= input->GetUsedQueues();
						_physicalDimensions[input->GetPhysicalIndex()].ImageUsage |= input->GetImageUsage();
					}

					if (pass.GetBlitTextureOutputs()[i]->GetPhysicalIndex() == RenderResource::Unused) {
						pass.GetBlitTextureOutputs()[i]->SetPhysicalIndex(input->GetPhysicalIndex());
					} else if (pass.GetBlitTextureOutputs()[i]->GetPhysicalIndex() != input->GetPhysicalIndex()) {
						throw std::logic_error("[RenderGraph] Cannot alias resources, index already claimed.");
					}
				}
			}
		}

		if (!pass.GetStorageTextureInputs().empty()) {
			uint32_t count = pass.GetStorageTextureInputs().size();
			for (uint32_t i = 0; i < count; ++i) {
				auto* input = pass.GetStorageTextureInputs()[i];
				if (input) {
					if (input->GetPhysicalIndex() == RenderResource::Unused) {
						_physicalDimensions.push_back(GetResourceDimensions(*input));
						input->SetPhysicalIndex(physicalIndex++);
					} else {
						_physicalDimensions[input->GetPhysicalIndex()].Queues |= input->GetUsedQueues();
						_physicalDimensions[input->GetPhysicalIndex()].ImageUsage |= input->GetImageUsage();
					}

					if (pass.GetStorageTextureOutputs()[i]->GetPhysicalIndex() == RenderResource::Unused) {
						pass.GetStorageTextureOutputs()[i]->SetPhysicalIndex(input->GetPhysicalIndex());
					} else if (pass.GetStorageTextureOutputs()[i]->GetPhysicalIndex() != input->GetPhysicalIndex()) {
						throw std::logic_error("[RenderGraph] Cannot alias resources, index already claimed.");
					}
				}
			}
		}

		// Finally, handle the proxy inputs.
		for (auto& input : pass.GetProxyInputs()) {
			if (input.Proxy->GetPhysicalIndex() == RenderResource::Unused) {
				ResourceDimensions dim = {};
				dim.Flags |= AttachmentInfoFlagBits::InternalProxy;
				_physicalDimensions.push_back(dim);
				input.Proxy->SetPhysicalIndex(physicalIndex++);
			} else {
				_physicalDimensions[input.Proxy->GetPhysicalIndex()].Queues |= input.Proxy->GetUsedQueues();
			}
		}

		// Now we go through the output attachments, and create physical resources for them as well, if they weren't able to
		// be aliased above.
		for (auto* output : pass.GetColorOutputs()) {
			if (output->GetPhysicalIndex() == RenderResource::Unused) {
				_physicalDimensions.push_back(GetResourceDimensions(*output));
				output->SetPhysicalIndex(physicalIndex++);
			} else {
				_physicalDimensions[output->GetPhysicalIndex()].Queues |= output->GetUsedQueues();
				_physicalDimensions[output->GetPhysicalIndex()].ImageUsage |= output->GetImageUsage();
			}
		}

		for (auto* output : pass.GetResolveOutputs()) {
			if (output->GetPhysicalIndex() == RenderResource::Unused) {
				_physicalDimensions.push_back(GetResourceDimensions(*output));
				output->SetPhysicalIndex(physicalIndex++);
			} else {
				_physicalDimensions[output->GetPhysicalIndex()].Queues |= output->GetUsedQueues();
				_physicalDimensions[output->GetPhysicalIndex()].ImageUsage |= output->GetImageUsage();
			}
		}

		for (auto* output : pass.GetStorageOutputs()) {
			if (output->GetPhysicalIndex() == RenderResource::Unused) {
				_physicalDimensions.push_back(GetResourceDimensions(*output));
				output->SetPhysicalIndex(physicalIndex++);
			} else {
				_physicalDimensions[output->GetPhysicalIndex()].Queues |= output->GetUsedQueues();
				_physicalDimensions[output->GetPhysicalIndex()].BufferInfo.Usage |= output->GetBufferUsage();
			}
		}

		for (auto& output : pass.GetProxyOutputs()) {
			if (output.Proxy->GetPhysicalIndex() == RenderResource::Unused) {
				ResourceDimensions dim = {};
				dim.Flags |= AttachmentInfoFlagBits::InternalProxy;
				_physicalDimensions.push_back(dim);
				output.Proxy->SetPhysicalIndex(physicalIndex++);
			} else {
				_physicalDimensions[output.Proxy->GetPhysicalIndex()].Queues |= output.Proxy->GetUsedQueues();
			}
		}

		for (auto* output : pass.GetTransferOutputs()) {
			if (output->GetPhysicalIndex() == RenderResource::Unused) {
				_physicalDimensions.push_back(GetResourceDimensions(*output));
				output->SetPhysicalIndex(physicalIndex++);
			} else {
				_physicalDimensions[output->GetPhysicalIndex()].Queues |= output->GetUsedQueues();
				_physicalDimensions[output->GetPhysicalIndex()].BufferInfo.Usage |= output->GetBufferUsage();
			}
		}

		for (auto* output : pass.GetBlitTextureOutputs()) {
			if (output->GetPhysicalIndex() == RenderResource::Unused) {
				_physicalDimensions.push_back(GetResourceDimensions(*output));
				output->SetPhysicalIndex(physicalIndex++);
			} else {
				_physicalDimensions[output->GetPhysicalIndex()].Queues |= output->GetUsedQueues();
				_physicalDimensions[output->GetPhysicalIndex()].ImageUsage |= output->GetImageUsage();
			}
		}

		for (auto* output : pass.GetStorageTextureOutputs()) {
			if (output->GetPhysicalIndex() == RenderResource::Unused) {
				_physicalDimensions.push_back(GetResourceDimensions(*output));
				output->SetPhysicalIndex(physicalIndex++);
			} else {
				_physicalDimensions[output->GetPhysicalIndex()].Queues |= output->GetUsedQueues();
				_physicalDimensions[output->GetPhysicalIndex()].ImageUsage |= output->GetImageUsage();
			}
		}

		// Now we take care of the depth/stencil attachments.
		auto* dsInput  = pass.GetDepthStencilInput();
		auto* dsOutput = pass.GetDepthStencilOutput();
		if (dsInput) {
			// If we have an input attachment, make sure it gets a physical resource.
			if (dsInput->GetPhysicalIndex() == RenderResource::Unused) {
				_physicalDimensions.push_back(GetResourceDimensions(*dsInput));
				dsInput->SetPhysicalIndex(physicalIndex++);
			} else {
				_physicalDimensions[dsInput->GetPhysicalIndex()].Queues |= dsInput->GetUsedQueues();
				_physicalDimensions[dsInput->GetPhysicalIndex()].ImageUsage |= dsInput->GetImageUsage();
			}

			// If we have an output attachment as well, make sure we can alias it.
			if (dsOutput) {
				if (dsOutput->GetPhysicalIndex() == RenderResource::Unused) {
					dsOutput->SetPhysicalIndex(dsInput->GetPhysicalIndex());
				} else if (dsOutput->GetPhysicalIndex() != dsInput->GetPhysicalIndex()) {
					throw std::logic_error("[RenderGraph] Cannot alias resources, index already claimed.");
				}

				_physicalDimensions[dsOutput->GetPhysicalIndex()].Queues |= dsOutput->GetUsedQueues();
				_physicalDimensions[dsOutput->GetPhysicalIndex()].ImageUsage |= dsOutput->GetImageUsage();
			}
		} else if (dsOutput) {
			// If we only have an output attachment, create it as normal.
			if (dsOutput->GetPhysicalIndex() == RenderResource::Unused) {
				_physicalDimensions.push_back(GetResourceDimensions(*dsOutput));
				dsOutput->SetPhysicalIndex(physicalIndex++);
			} else {
				_physicalDimensions[dsOutput->GetPhysicalIndex()].Queues |= dsOutput->GetUsedQueues();
				_physicalDimensions[dsOutput->GetPhysicalIndex()].ImageUsage |= dsOutput->GetImageUsage();
			}
		}

		// Handle the input attachments last, so they can alias with any color or depth/stencil attachments we've already
		// made where possible.
		for (auto* input : pass.GetAttachmentInputs()) {
			if (input->GetPhysicalIndex() == RenderResource::Unused) {
				_physicalDimensions.push_back(GetResourceDimensions(*input));
				input->SetPhysicalIndex(physicalIndex++);
			} else {
				_physicalDimensions[input->GetPhysicalIndex()].Queues |= input->GetUsedQueues();
				_physicalDimensions[input->GetPhysicalIndex()].ImageUsage |= input->GetImageUsage();
			}
		}

		// Finally, make note of the "fake" resources we should be aliasing.
		for (auto& pair : pass.GetFakeResourceAliases()) { pair.second->SetPhysicalIndex(pair.first->GetPhysicalIndex()); }
	}

	// Now that we have all our physical indices, we need to determine which physical images are used for history input.
	_physicalImageHasHistory.clear();
	_physicalImageHasHistory.resize(_physicalDimensions.size());
	for (auto& passIndex : _passStack) {
		auto& pass = *_passes[passIndex];
		for (auto& history : pass.GetHistoryInputs()) {
			if (history->GetPhysicalIndex() == RenderResource::Unused) {
				throw std::logic_error("[RenderGraph] History input is used, but never written to.");
			}
			_physicalImageHasHistory[history->GetPhysicalIndex()] = true;
		}
	}
}

void RenderGraph::BuildRenderPassInfo() {
	for (uint32_t physicalPassIndex = 0; physicalPassIndex < _physicalPasses.size(); ++physicalPassIndex) {
		auto& physicalPass = _physicalPasses[physicalPassIndex];

		physicalPass.ColorClearRequests.clear();
		physicalPass.DepthClearRequest = {};

		// Initialize our Render Pass info.
		auto& rp               = physicalPass.RenderPassInfo;
		rp.ClearAttachmentMask = 0;
		rp.LoadAttachmentMask  = 0;
		rp.StoreAttachmentMask = ~0u;
		rp.Subpasses.resize(physicalPass.Passes.size());

		auto& colors = physicalPass.PhysicalColorAttachments;
		colors.clear();

		/**
		 * Retrieves an index for a color attachment. If the resource has not been used, it will add it to the list. If the
		 * resource has been used, it will return the existing index. If the resource is new, the second output will be
		 * true. If it has been used, the second output will be false.
		 */
		auto AddUniqueColor = [&](uint32_t index) -> std::pair<uint32_t, bool> {
			auto it = std::find(colors.begin(), colors.end(), index);
			if (it != colors.end()) { return std::make_pair(uint32_t(it - colors.begin()), false); }

			uint32_t ret = colors.size();
			colors.push_back(index);
			return std::make_pair(ret, true);
		};
		auto AddUniqueInputAttachment = [&](uint32_t index) -> std::pair<uint32_t, bool> {
			if (index == physicalPass.PhysicalDepthStencilAttachment) {
				return std::make_pair(uint32_t(colors.size()), false);
			} else {
				return AddUniqueColor(index);
			}
		};

		for (uint32_t subpassIndex = 0; subpassIndex < physicalPass.Passes.size(); ++subpassIndex) {
			auto& pass = *_passes[physicalPass.Passes[subpassIndex]];

			// Add all of our color attachments.
			const uint32_t colorAttachmentCount = pass.GetColorOutputs().size();
			std::vector<ScaledClearRequest> scaledClearRequests;
			rp.Subpasses[subpassIndex].ColorAttachmentCount = colorAttachmentCount;
			for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
				auto res                                       = AddUniqueColor(pass.GetColorOutputs()[i]->GetPhysicalIndex());
				rp.Subpasses[subpassIndex].ColorAttachments[i] = res.first;

				if (res.second) {
					// If this is the first time the attachment has been used, we need to determine its load op.
					// If there's no color input, we can safely clear it.
					// If it needs to be scaled, we queue up a scaled clear request.
					// If it has a color input but no scaling, we have to load.

					const bool hasColorInput       = !pass.GetColorInputs().empty() && pass.GetColorInputs()[i];
					const bool hasScaledColorInput = !pass.GetColorScaleInputs().empty() && pass.GetColorScaleInputs()[i];

					if (!hasColorInput && !hasScaledColorInput) {
						if (pass.GetClearColor(i)) {
							rp.ClearAttachmentMask |= 1u << res.first;
							physicalPass.ColorClearRequests.push_back({&pass, &rp.ColorClearValues[res.first], i});
						}
					} else {
						if (hasScaledColorInput) {
							scaledClearRequests.push_back({i, pass.GetColorScaleInputs()[i]->GetPhysicalIndex()});
						} else {
							rp.LoadAttachmentMask |= 1u << res.first;
						}
					}
				}
			}
			physicalPass.ScaledClearRequests.push_back(std::move(scaledClearRequests));

			// Add our resolve outputs.
			if (!pass.GetResolveOutputs().empty()) {
				rp.Subpasses[subpassIndex].ResolveAttachmentCount = colorAttachmentCount;
				for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
					auto res = AddUniqueColor(pass.GetResolveOutputs()[i]->GetPhysicalIndex());
					rp.Subpasses[subpassIndex].ResolveAttachments[i] = res.first;

					// Resolve outputs are always don't care, so we don't bother looking if it was the first time it was used.
				}
			}

			// Add our depth-stencil input and output.
			auto* dsInput    = pass.GetDepthStencilInput();
			auto* dsOutput   = pass.GetDepthStencilOutput();
			auto AddUniqueDS = [&](uint32_t index) -> std::pair<uint32_t, bool> {
				bool newAttachment = physicalPass.PhysicalDepthStencilAttachment == RenderResource::Unused;
				physicalPass.PhysicalDepthStencilAttachment = index;
				return std::make_pair(index, newAttachment);
			};
			if (dsInput && dsOutput) {
				auto res = AddUniqueDS(dsOutput->GetPhysicalIndex());

				// If this is the first use, we must load.
				if (res.second) { rp.LoadAttachmentMask |= 1u << res.first; }

				rp.Flags |= Vulkan::RenderPassOpFlagBits::StoreDepthStencil;
				rp.Subpasses[subpassIndex].DepthStencil = Vulkan::RenderPassInfo::DepthStencilUsage::ReadWrite;
			} else if (dsOutput) {
				auto res = AddUniqueDS(dsOutput->GetPhysicalIndex());

				// If this is the first use and we have a clear value, clear it.
				if (res.second && pass.GetClearDepthStencil()) {
					rp.Flags |= Vulkan::RenderPassOpFlagBits::ClearDepthStencil;
					physicalPass.DepthClearRequest.Pass   = &pass;
					physicalPass.DepthClearRequest.Target = &rp.DepthStencilClearValue;
				}

				rp.Flags |= Vulkan::RenderPassOpFlagBits::StoreDepthStencil;
				rp.Subpasses[subpassIndex].DepthStencil     = Vulkan::RenderPassInfo::DepthStencilUsage::ReadWrite;
				physicalPass.PhysicalDepthStencilAttachment = dsOutput->GetPhysicalIndex();
			} else if (dsInput) {
				auto res = AddUniqueDS(dsInput->GetPhysicalIndex());

				if (res.second) {
					// If this is the first use, we need to load.
					rp.Flags |=
						Vulkan::RenderPassOpFlagBits::DepthStencilReadOnly | Vulkan::RenderPassOpFlagBits::LoadDepthStencil;

					// We also need to see if this needs to be stored again, for use in a later pass.
					const auto CheckPreserve = [this, physicalPassIndex](const RenderResource& tex) -> bool {
						for (auto& readPass : tex.GetReadPasses()) {
							if (_passes[readPass]->GetPhysicalPassIndex() > physicalPassIndex) { return true; }
						}

						return false;
					};

					bool preserveDepth = CheckPreserve(*dsInput);
					if (!preserveDepth) {
						for (auto& logicalPass : _passes) {
							for (auto& alias : logicalPass->GetFakeResourceAliases()) {
								if (alias.first == dsInput && CheckPreserve(*alias.second)) {
									preserveDepth = true;
									break;
								}
							}
						}
					}

					// If this input is used later, make sure it's stored too.
					if (preserveDepth) { rp.Flags |= Vulkan::RenderPassOpFlagBits::StoreDepthStencil; }
				}

				rp.Subpasses[subpassIndex].DepthStencil = Vulkan::RenderPassInfo::DepthStencilUsage::ReadOnly;
			} else {
				rp.Subpasses[subpassIndex].DepthStencil = Vulkan::RenderPassInfo::DepthStencilUsage::None;
			}
		}

		// Separate loop for input attachments, to make sure we've handled all depth/stencil attachments first.
		for (uint32_t subpassIndex = 0; subpassIndex < physicalPass.Passes.size(); ++subpassIndex) {
			auto& pass = *_passes[physicalPass.Passes[subpassIndex]];

			uint32_t inputAttachmentCount                   = pass.GetAttachmentInputs().size();
			rp.Subpasses[subpassIndex].InputAttachmentCount = inputAttachmentCount;
			for (uint32_t i = 0; i < inputAttachmentCount; ++i) {
				auto res = AddUniqueInputAttachment(pass.GetAttachmentInputs()[i]->GetPhysicalIndex());
				rp.Subpasses[subpassIndex].InputAttachments[i] = res.first;

				// If this is the first time it's used, we need to load.
				if (res.second) { rp.LoadAttachmentMask |= 1u << res.first; }
			}
		}

		physicalPass.RenderPassInfo.ColorAttachmentCount = physicalPass.PhysicalColorAttachments.size();
	}
}

void RenderGraph::BuildTransients() {
	std::vector<uint32_t> physicalPassUsed(_physicalDimensions.size(), RenderPass::Unused);

	// First, strip away the transient flag for anything that is not allowed to be transient.
	for (uint32_t i = 0; i < _physicalDimensions.size(); ++i) {
		auto& dim = _physicalDimensions[i];
		// Buffers and Storage Images can never be transient.
		if (dim.IsBufferLike()) {
			dim.Flags &= ~AttachmentInfoFlagBits::InternalTransient;
		} else {
			dim.Flags |= AttachmentInfoFlagBits::InternalTransient;
		}

		// History images also can never be transient, by nature.
		if (_physicalImageHasHistory[i]) { dim.Flags &= ~AttachmentInfoFlagBits::InternalTransient; }
	}

	for (auto& resource : _resources) {
		// Only textures can be transient.
		if (resource->GetType() != RenderResource::Type::Texture) { continue; }

		const uint32_t physicalIndex = resource->GetPhysicalIndex();
		// If this resource is unused, it can't be aliased, of course.
		if (physicalIndex == RenderResource::Unused) { continue; }

		// If this image was written to in more than one physical pass, it cannot be transient.
		for (auto& pass : resource->GetWritePasses()) {
			uint32_t physicalPassIndex = _passes[pass]->GetPhysicalPassIndex();
			if (physicalPassIndex != RenderResource::Unused) {
				if (physicalPassUsed[physicalIndex] != RenderPass::Unused &&
				    physicalPassIndex != physicalPassUsed[physicalIndex]) {
					_physicalDimensions[physicalIndex].Flags &= ~AttachmentInfoFlagBits::InternalTransient;
					break;
				}

				physicalPassUsed[physicalIndex] = physicalPassIndex;
			}
		}

		// If this image was read from in more than one physical pass, it cannot be transient.
		for (auto& pass : resource->GetReadPasses()) {
			uint32_t physicalPassIndex = _passes[pass]->GetPhysicalPassIndex();
			if (physicalPassIndex != RenderResource::Unused) {
				if (physicalPassUsed[physicalIndex] != RenderPass::Unused &&
				    physicalPassIndex != physicalPassUsed[physicalIndex]) {
					_physicalDimensions[physicalIndex].Flags &= ~AttachmentInfoFlagBits::InternalTransient;
					break;
				}

				physicalPassUsed[physicalIndex] = physicalPassIndex;
			}
		}
	}
}

void RenderGraph::DependPassesRecursive(const RenderPass& self,
                                        const std::unordered_set<uint32_t>& passes,
                                        uint32_t depth,
                                        bool noCheck,
                                        bool ignoreSelf,
                                        bool mergeDependency) {
	// Ensure the resource is written to.
	if (!noCheck && passes.empty()) { throw std::logic_error("[RenderGraph] Used resource is never written to."); }

	// Ensure we haven't gone into a loop.
	if (depth > _passes.size()) { throw std::logic_error("[RenderGraph] Cyclic dependency detected."); }

	// Add to the list of render passes that we depend on.
	// If it's a possible merge candidate, keep track of that too.
	for (auto& pass : passes) {
		if (pass != self.GetIndex()) {
			_passDependencies[self.GetIndex()].insert(pass);

			if (mergeDependency) { _passMergeDependencies[self.GetIndex()].insert(pass); }
		}
	}

	depth++;

	// Recurse down the list of the render passes we depend on.
	for (auto& pass : passes) {
		// Ensure we don't recurse on ourselves.
		if (pass == self.GetIndex()) {
			if (ignoreSelf) {
				continue;
			} else {
				throw std::logic_error("[RenderGraph] Render Pass depends on itself.");
			}
		}

		_passStack.push_back(pass);
		TraverseDependencies(*_passes[pass], depth);
	}
}

void RenderGraph::EnqueueRenderPass(Vulkan::Device& device,
                                    PhysicalPass& physicalPass,
                                    PassSubmissionState& state,
                                    TaskComposer& composer) {
	if (!PhysicalPassRequiresWork(physicalPass)) {
		PhysicalPassTransferOwnership(physicalPass);
		return;
	}

	state.Active = true;
	PhysicalPassHandleCPU(device, physicalPass, state, composer);
}

void RenderGraph::FilterPasses() {
	std::unordered_set<uint32_t> seen;
	auto outputIt = _passStack.begin();
	for (auto it = _passStack.begin(); it != _passStack.end(); ++it) {
		if (!seen.count(*it)) {
			*outputIt = *it;
			seen.insert(*it);
			++outputIt;
		}
	}
	_passStack.erase(outputIt, _passStack.end());
}

void RenderGraph::GetQueueType(Vulkan::CommandBufferType& queueType,
                               bool& graphics,
                               RenderGraphQueueFlagBits flag) const {
	switch (flag) {
		default:
		case RenderGraphQueueFlagBits::Graphics:
			graphics  = true;
			queueType = Vulkan::CommandBufferType::Generic;
			break;

		case RenderGraphQueueFlagBits::Compute:
			graphics  = false;
			queueType = Vulkan::CommandBufferType::Generic;
			break;

		case RenderGraphQueueFlagBits::AsyncCompute:
			graphics  = false;
			queueType = Vulkan::CommandBufferType::AsyncCompute;
			break;

		case RenderGraphQueueFlagBits::AsyncGraphics:
			graphics  = true;
			queueType = Vulkan::CommandBufferType::AsyncGraphics;
			break;
	}
}

void RenderGraph::PhysicalPassEnqueueGraphicsCommands(const PhysicalPass& physicalPass, PassSubmissionState& state) {
	auto& cmd = *state.Cmd;

	for (auto& clearReq : physicalPass.ColorClearRequests) {
		clearReq.Pass->GetClearColor(clearReq.Index, clearReq.Target);
	}
	if (physicalPass.DepthClearRequest.Pass) {
		physicalPass.DepthClearRequest.Pass->GetClearDepthStencil(physicalPass.DepthClearRequest.Target);
	}

	auto rpInfo = physicalPass.RenderPassInfo;

	uint32_t layerIterations = 1;
	if (physicalPass.Layers > 1) {}

	for (uint32_t layer = 0; layer < layerIterations; ++layer) {
		rpInfo.BaseLayer = layer;
		cmd.BeginRenderPass(rpInfo, state.SubpassContents[0]);

		for (auto& subpass : physicalPass.Passes) {
			auto subpassIndex    = uint32_t(&subpass - physicalPass.Passes.data());
			auto& scaledRequests = physicalPass.ScaledClearRequests[subpassIndex];

			auto& pass = *_passes[subpass];
			pass.BuildRenderPass(cmd, layer);

			if (&subpass != &physicalPass.Passes.back()) { cmd.NextSubpass(state.SubpassContents[subpassIndex + 1]); }
		}

		cmd.EndRenderPass();
	}
}

void RenderGraph::PhysicalPassHandleCPU(Vulkan::Device& device,
                                        const PhysicalPass& physicalPass,
                                        PassSubmissionState& state,
                                        TaskComposer& incomingComposer) {
	GetQueueType(state.QueueType, state.Graphics, _passes[physicalPass.Passes.front()]->GetQueue());
	PhysicalPassInvalidateAttachments(physicalPass);
	for (auto& barrier : physicalPass.Invalidate) {
		bool physicalGraphics = device.GetQueueType(state.QueueType) == Vulkan::QueueType::Graphics;
		PhysicalPassInvalidateBarrier(barrier, state, physicalGraphics);
	}
	PhysicalPassHandleSignal(device, physicalPass, state);
	for (auto& barrier : physicalPass.Flush) { PhysicalPassHandleFlushBarrier(barrier, state); }
	PhysicalPassTransferOwnership(physicalPass);

	state.SubpassContents.resize(physicalPass.Passes.size());
	std::fill(state.SubpassContents.begin(), state.SubpassContents.end(), vk::SubpassContents::eInline);

	TaskComposer composer;
	composer.SetIncomingTask(incomingComposer.GetPipelineStageDependency());
	composer.BeginPipelineStage();
	for (auto& pass : physicalPass.Passes) {
		auto& subpass = *_passes[pass];
		subpass.PrepareRenderPass(composer);
	}
	state.RenderingDependency = composer.GetOutgoingTask();
}

void RenderGraph::PhysicalPassHandleFlushBarrier(const Barrier& barrier, PassSubmissionState& state) {}

void RenderGraph::PhysicalPassHandleGPU(Vulkan::Device& device, const PhysicalPass& pass, PassSubmissionState& state) {
	auto* threading = Threading::Get();
	auto group      = threading->CreateTaskGroup();

	group->Enqueue([&]() {
		state.Cmd = device.RequestCommandBuffer(state.QueueType);

		if (state.Graphics) {
			PhysicalPassEnqueueGraphicsCommands(pass, state);
		} else {
		}
	});

	if (state.RenderingDependency) { threading->AddDependency(*group, *state.RenderingDependency); }
	state.RenderingDependency = group;
}

void RenderGraph::PhysicalPassHandleSignal(Vulkan::Device& device,
                                           const PhysicalPass& physicalPass,
                                           PassSubmissionState& state) {}

void RenderGraph::PhysicalPassInvalidateAttachments(const PhysicalPass& physicalPass) {
	for (auto& discard : physicalPass.Discards) {
		if (!_physicalDimensions[discard].IsBufferLike()) { _physicalEvents[discard].Layout = vk::ImageLayout::eUndefined; }
	}
}

void RenderGraph::PhysicalPassInvalidateBarrier(const Barrier& barrier,
                                                PassSubmissionState& state,
                                                bool physicalGraphics) {}

bool RenderGraph::PhysicalPassRequiresWork(const PhysicalPass& physicalPass) const {
	for (auto& pass : physicalPass.Passes) {
		if (_passes[pass]->NeedRenderPass()) { return true; }
	}

	return false;
}

void RenderGraph::PhysicalPassTransferOwnership(const PhysicalPass& physicalPass) {
	for (auto& transfer : physicalPass.AliasTransfer) {
		auto& physEvents = _physicalEvents[transfer.second];
		physEvents       = _physicalEvents[transfer.first];
		for (auto& e : physEvents.InvalidatedInStage) { e = {}; }
		physEvents.ToFlushAccess = {};
		physEvents.Layout        = vk::ImageLayout::eUndefined;
	}
}

void RenderGraph::ReorderPasses() {}

void RenderGraph::TraverseDependencies(const RenderPass& pass, uint32_t depth) {
	// Ensure we check Depth/Stencil, Input, and Color attachments first, as they are important to determining if Render
	// Passes can be merged.
	if (pass.GetDepthStencilInput()) {
		DependPassesRecursive(pass, pass.GetDepthStencilInput()->GetWritePasses(), depth, false, false, true);
	}

	for (auto* input : pass.GetAttachmentInputs()) {
		bool selfDependency = pass.GetDepthStencilOutput() == input;
		if (std::find(pass.GetColorOutputs().begin(), pass.GetColorOutputs().end(), input) !=
		    pass.GetColorOutputs().end()) {
			selfDependency = true;
		}

		if (!selfDependency) { DependPassesRecursive(pass, input->GetWritePasses(), depth, false, false, true); }
	}

	for (auto* input : pass.GetColorInputs()) {
		if (input) { DependPassesRecursive(pass, input->GetWritePasses(), depth, false, false, true); }
	}

	// Now check the other input attachment types.
	for (auto* input : pass.GetColorScaleInputs()) {
		if (input) { DependPassesRecursive(pass, input->GetWritePasses(), depth, false, false, false); }
	}
	for (auto* input : pass.GetBlitTextureInputs()) {
		if (input) { DependPassesRecursive(pass, input->GetWritePasses(), depth, false, false, false); }
	}
	for (auto* input : pass.GetStorageTextureInputs()) {
		if (input) { DependPassesRecursive(pass, input->GetWritePasses(), depth, false, false, false); }
	}
	for (auto& input : pass.GetGenericTextureInputs()) {
		DependPassesRecursive(pass, input.Texture->GetWritePasses(), depth, false, false, false);
	}
	for (auto& input : pass.GetProxyInputs()) {
		DependPassesRecursive(pass, input.Proxy->GetWritePasses(), depth, false, false, false);
	}

	// Check the storage buffer inputs next.
	for (auto* input : pass.GetStorageInputs()) {
		if (input) {
			// Storage buffers may be used as feedback, so ignore it if nothing appears to write to them.
			DependPassesRecursive(pass, input->GetWritePasses(), depth, true, false, false);

			// Ensure Write-After-Read hazards are handled if the buffer is read from in another pass.
			DependPassesRecursive(pass, input->GetReadPasses(), depth, true, true, false);
		}
	}

	for (auto& input : pass.GetGenericBufferInputs()) {
		// Storage buffers may be used as feedback, so ignore it if nothing appears to write to them.
		DependPassesRecursive(pass, input.Buffer->GetWritePasses(), depth, true, false, false);
	}
}

void RenderGraph::ValidatePasses() {
	for (auto& passPtr : _passes) {
		auto& pass = *passPtr;

		// Every blit output must have a matching blit input.
		if (pass.GetBlitTextureInputs().size() != pass.GetBlitTextureOutputs().size()) {
			throw std::logic_error("[RenderGraph] Size of blit texture inputs must match blit texture outputs.");
		}
		// Every color output must have a matching color input.
		if (pass.GetColorInputs().size() != pass.GetColorOutputs().size()) {
			throw std::logic_error("[RenderGraph] Size of color inputs must match color outputs.");
		}
		// Every storage output must have a matching storage input.
		if (pass.GetStorageInputs().size() != pass.GetStorageOutputs().size()) {
			throw std::logic_error("[RenderGraph] Size of storage inputs must match storage outputs.");
		}
		// Every storage texture output must have a matching storage texture input.
		if (pass.GetStorageTextureInputs().size() != pass.GetStorageTextureOutputs().size()) {
			throw std::logic_error("[RenderGraph] Size of storage texture inputs must match storage texture outputs.");
		}
		// If we have any resolve outputs, there must be one for each color output.
		if (!pass.GetResolveOutputs().empty() && pass.GetResolveOutputs().size() != pass.GetColorOutputs().size()) {
			throw std::logic_error("[RenderGraph] Must have one resolve output for each color output.");
		}

		// For each color output, if the input is not the same size, ensure it is added to the scaled input list.
		uint32_t inputCount = pass.GetColorInputs().size();
		for (uint32_t i = 0; i < inputCount; ++i) {
			if (!pass.GetColorInputs()[i]) { continue; }

			if (GetResourceDimensions(*pass.GetColorInputs()[i]) != GetResourceDimensions(*pass.GetColorOutputs()[i])) {
				pass.MakeColorInputScaled(i);
			}
		}

		// Ensure both buffers used in RMW operations are identical in size and usage.
		if (!pass.GetStorageOutputs().empty()) {
			uint32_t outputCount = pass.GetStorageOutputs().size();
			for (uint32_t i = 0; i < outputCount; ++i) {
				if (!pass.GetStorageInputs()[i]) { continue; }

				if (pass.GetStorageOutputs()[i]->GetBufferInfo() != pass.GetStorageInputs()[i]->GetBufferInfo()) {
					throw std::logic_error("[RenderGraph] Performing RMW on incompatible storage buffers.");
				}
			}
		}

		// Ensure both images used in blit operations have identical parameters.
		if (!pass.GetBlitTextureOutputs().empty()) {
			uint32_t outputCount = pass.GetBlitTextureOutputs().size();
			for (uint32_t i = 0; i < outputCount; ++i) {
				if (!pass.GetBlitTextureInputs()[i]) { continue; }

				if (GetResourceDimensions(*pass.GetBlitTextureInputs()[i]) !=
				    GetResourceDimensions(*pass.GetBlitTextureOutputs()[i])) {
					throw std::logic_error("[RenderGraph] Doing RMW on incompatible blit textures.");
				}
			}
		}

		// Ensure both images used in storage texture operations have identical parameters.
		if (!pass.GetStorageTextureOutputs().empty()) {
			uint32_t outputCount = pass.GetStorageTextureOutputs().size();
			for (uint32_t i = 0; i < outputCount; ++i) {
				if (!pass.GetStorageTextureInputs()[i]) { continue; }

				if (GetResourceDimensions(*pass.GetStorageTextureInputs()[i]) !=
				    GetResourceDimensions(*pass.GetStorageTextureOutputs()[i])) {
					throw std::logic_error("[RenderGraph] Doing RMW on incompatible storage textures.");
				}
			}
		}

		// Ensure depth/stencil input and output have identical parameters.
		if (pass.GetDepthStencilInput() && pass.GetDepthStencilOutput()) {
			if (GetResourceDimensions(*pass.GetDepthStencilInput()) != GetResourceDimensions(*pass.GetDepthStencilOutput())) {
				throw std::logic_error("[RenderGraph] Depth Stencil input/output mismatch.");
			}
		}
	}
}

void RenderGraph::PassSubmissionState::Submit() {
	if (!Cmd) { return; }

	auto& device = Cmd->GetDevice();
	device.Submit(Cmd);
}
}  // namespace Luna
