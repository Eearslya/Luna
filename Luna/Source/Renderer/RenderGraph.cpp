#include <Luna/Renderer/RenderGraph.hpp>
#include <Luna/Renderer/RenderPass.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Utility/BitOps.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/Semaphore.hpp>

namespace Luna {
static constexpr RenderGraphQueueFlags ComputeQueues =
	RenderGraphQueueFlagBits::Compute | RenderGraphQueueFlagBits::AsyncCompute;

RenderGraph::RenderGraph() {}

RenderGraph::~RenderGraph() noexcept {}

void RenderGraph::EnqueueRenderPasses(Vulkan::Device& device, TaskComposer& composer) {
	const auto count = _physicalPasses.size();
	_passSubmissionStates.clear();
	_passSubmissionStates.resize(count);

	for (size_t i = 0; i < count; ++i) {
		EnqueueRenderPass(device, _physicalPasses[i], _passSubmissionStates[i], composer);
	}

	for (size_t i = 0; i < count; ++i) {
		if (_passSubmissionStates[i].Active) {
			EnqueuePhysicalPassGPU(device, _physicalPasses[i], _passSubmissionStates[i]);
		}
	}

	for (auto& state : _passSubmissionStates) {
		auto& group = composer.BeginPipelineStage();
		if (state.RenderingDependency) {
			Threading::AddDependency(group, *state.RenderingDependency);
			state.RenderingDependency.Reset();
		}

		group.Enqueue([&state, &device]() { state.Submit(device); });
	}

	if (_backbufferPhysicalIndex == RenderResource::Unused) {
		// TODO Swapchain scale pass
		Log::Warning("RenderGraph", "No swapchain scale pass");
	} else {
		auto& group = composer.BeginPipelineStage();
		group.Enqueue([&device]() { device.FlushFrame(); });
	}
}

void RenderGraph::SetupAttachments(Vulkan::Device& device, Vulkan::ImageView* backbuffer) {
	_physicalAttachments.clear();
	_physicalAttachments.resize(_physicalDimensions.size());
	_physicalBuffers.resize(_physicalDimensions.size());
	_physicalImages.resize(_physicalDimensions.size());
	_physicalHistoryImages.resize(_physicalDimensions.size());
	_physicalEvents.resize(_physicalDimensions.size());
	_physicalHistoryEvents.resize(_physicalDimensions.size());

	_backbufferAttachment = backbuffer;

	const uint32_t attachmentCount = _physicalDimensions.size();
	for (uint32_t i = 0; i < attachmentCount; ++i) {
		if (_physicalImageHasHistory[i]) {
			std::swap(_physicalHistoryImages[i], _physicalImages[i]);
			std::swap(_physicalHistoryEvents[i], _physicalEvents[i]);
		}

		auto& att = _physicalDimensions[i];
		if (att.Flags & AttachmentInfoFlagBits::InternalProxy) { continue; }

		if (att.BufferInfo.Size != 0) {
			SetupPhysicalBuffer(device, i);
		} else {
			if (att.IsStorageImage()) {
				SetupPhysicalImage(device, i);
			} else if (i == _backbufferPhysicalIndex) {
				_physicalAttachments[i] = backbuffer;
			} else if (att.Flags & AttachmentInfoFlagBits::InternalTransient) {
				_physicalImages[i] = device.RequestTransientAttachment(
					vk::Extent2D(att.Width, att.Height), att.Format, i, vk::SampleCountFlagBits::e1, att.ArrayLayers);
				_physicalAttachments[i] = &_physicalImages[i]->GetView();
			} else {
				SetupPhysicalImage(device, i);
			}
		}
	}

	for (auto& physicalPass : _physicalPasses) {
		uint32_t layers = ~0u;

		const uint32_t colorAttachmentCount = physicalPass.PhysicalColorAttachments.size();
		for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
			auto& att = physicalPass.RenderPassInfo.ColorAttachments[i];
			att       = _physicalAttachments[physicalPass.PhysicalColorAttachments[i]];

			if (att->GetImage().GetCreateInfo().Domain == Vulkan::ImageDomain::Physical) {
				layers = std::min(layers, att->GetImage().GetCreateInfo().ArrayLayers);
			}
		}

		if (physicalPass.PhysicalDepthStencilAttachment != RenderResource::Unused) {
			auto& ds = physicalPass.RenderPassInfo.DepthStencilAttachment;
			ds       = _physicalAttachments[physicalPass.PhysicalDepthStencilAttachment];

			if (ds->GetImage().GetCreateInfo().Domain == Vulkan::ImageDomain::Physical) {
				layers = std::min(layers, ds->GetImage().GetCreateInfo().ArrayLayers);
			}
		} else {
			physicalPass.RenderPassInfo.DepthStencilAttachment = nullptr;
		}

		physicalPass.ArrayLayers = layers;
	}
}

void RenderGraph::Bake(Vulkan::Device& device) {
	// Clean up the previous baked information, if any.
	_passStack.clear();
	_passDependencies.clear();
	_passMergeDependencies.clear();
	_passDependencies.resize(_passes.size());
	_passMergeDependencies.resize(_passes.size());

	// Ensure our backbuffer exists and is written to.
	auto it = _resourceToIndex.find(_backbufferSource);
	if (it == _resourceToIndex.end()) { throw std::logic_error("[RenderGraph] Backbuffer source does not exist."); }
	auto& backbufferResource = *_resources[it->second];
	if (backbufferResource.GetWritePasses().empty()) {
		throw std::logic_error("[RenderGraph] Backbuffer source is never written to.");
	}

	// Allow the Render Passes a chance to set up their dependencies.
	for (auto& pass : _passes) { pass->SetupDependencies(); }

	// Ensure the Render Graph is sane.
	ValidatePasses();

	// We start building the graph from the bottom up.
	// The first step is to look at the backbuffer, and add all of the render passes which write to it.
	for (auto passIndex : backbufferResource.GetWritePasses()) { _passStack.push_back(passIndex); }

	// Next, we walk the graph and add all of the dependent render passes, and their dependencies, and so on.
	auto tmpPassStack = _passStack;  // We need a copy, because we will be modifying _passStack during iteration.
	for (auto passIndex : tmpPassStack) { TraverseDependencies(*_passes[passIndex], 0); }

	// We started at the bottom and worked our way to the top. Now we need to flip the stack so we can start at the
	// beginning.
	std::reverse(_passStack.begin(), _passStack.end());

	// Ensure each Render Pass only appears in the stack once.
	FilterPasses();

	// Reorder the render passes so that we're running as many things in parellel as possible.
	ReorderPasses();

	// We now have our completed list of render passes, arranged in the correct order.
	// Now we begin to build our physical resources and render passes.

	// Starting with the resources, we perform simple aliasing where possible. e.g. Aliasing a depth/stencil input and
	// output to the same physical image.
	BuildPhysicalResources();

	// Now we build our physical render passes. If possible, we try to merge multiple logical render passes into one.
	BuildPhysicalPasses();

	// After merging everything we can, if an image is only used in one physical pass, make it transient.
	BuildTransients();

	// Now we can build our actual render pass info.
	BuildRenderPassInfo();

	// Determine the barriers needed for each render pass in isolation.
	BuildBarriers();

	// Set up our backbuffer resource.
	_backbufferPhysicalIndex = backbufferResource.GetPhysicalIndex();
	auto& backbufferDim      = _physicalDimensions[_backbufferPhysicalIndex];
	bool canAliasBackbuffer =
		!bool(backbufferDim.Queues & ComputeQueues) && backbufferDim.Flags & AttachmentInfoFlagBits::InternalTransient;
	for (auto& dim : _physicalDimensions) {
		if (&dim != &backbufferDim) { dim.Transform = vk::SurfaceTransformFlagBitsKHR::eIdentity; }
	}
	backbufferDim.Flags &= ~(AttachmentInfoFlagBits::InternalTransient | AttachmentInfoFlagBits::SupportsPrerotate);
	backbufferDim.Flags |= _backbufferDimensions.Flags & AttachmentInfoFlagBits::Persistent;
	if (!canAliasBackbuffer || backbufferDim != _backbufferDimensions) {
		_backbufferPhysicalIndex = RenderResource::Unused;
		if (!bool(backbufferDim.Queues & RenderGraphQueueFlagBits::Graphics)) {
			backbufferDim.Queues |= RenderGraphQueueFlagBits::AsyncGraphics;
		} else {
			backbufferDim.Queues |= RenderGraphQueueFlagBits::Graphics;
		}

		backbufferDim.ImageUsage |= vk::ImageUsageFlagBits::eSampled;
		backbufferDim.Transform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
	} else {
		_physicalDimensions[_backbufferPhysicalIndex].Flags |= AttachmentInfoFlagBits::InternalTransient;
	}

	BuildPhysicalBarriers();

	BuildAliases();

	for (auto& physicalPass : _physicalPasses) {
		for (auto passIndex : physicalPass.Passes) { _passes[passIndex]->Setup(); }
	}
}

void RenderGraph::Log() {
	Log::Debug("RenderGraph", "===== Baked Render Graph Information =====");

	Log::Debug("RenderGraph", "Physical Resources ({}):", _physicalDimensions.size());
	for (uint32_t i = 0; i < _physicalDimensions.size(); ++i) {
		const auto& resource = _physicalDimensions[i];

		if (resource.BufferInfo.Size) {
			Log::Debug("RenderGraph", "- Buffer #{} ({}):", i, resource.Name);
			Log::Debug("RenderGraph", "  - Size: {}", resource.BufferInfo.Size);
			Log::Debug("RenderGraph", "  - Usage: {}", vk::to_string(resource.BufferInfo.Usage));
		} else {
			Log::Debug(
				"RenderGraph", "- Texture #{} ({}):{}", i, resource.Name, i == _backbufferPhysicalIndex ? " (Swapchain)" : "");
			Log::Debug("RenderGraph", "  - Format: {}", vk::to_string(resource.Format));
			Log::Debug("RenderGraph", "  - Extent: {}x{}x{}", resource.Width, resource.Height, resource.Depth);
			Log::Debug("RenderGraph",
			           "  - Layers: {}, Levels: {}, Samples: {}",
			           resource.ArrayLayers,
			           resource.MipLevels,
			           resource.SampleCount);
			Log::Debug("RenderGraph", "  - Usage: {}", vk::to_string(resource.ImageUsage));
			Log::Debug(
				"RenderGraph", "  - Transient: {}", resource.Flags & AttachmentInfoFlagBits::InternalTransient ? "Yes" : "No");
		}
	}

	const auto Resource = [&](uint32_t physicalIndex) {
		const auto& dim = _physicalDimensions[physicalIndex];
		return std::format("{} ({})", physicalIndex, dim.Name);
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
				    barrier.ResourceIndex != _backbufferPhysicalIndex) {
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
	_passSubmissionStates.clear();
	_physicalPasses.clear();
	_physicalDimensions.clear();
	_physicalAttachments.clear();
	_physicalBuffers.clear();
	_physicalImages.clear();
	_physicalEvents.clear();
	_physicalHistoryEvents.clear();
	_physicalHistoryImages.clear();
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
	dim.ImageUsage  = info.AuxUsage | resource.GetImageUsage();
	dim.ArrayLayers = info.ArrayLayers;
	dim.Name        = resource.GetName();
	dim.Queues      = resource.GetUsedQueues();
	dim.SampleCount = info.SampleCount;

	if (info.Flags & AttachmentInfoFlagBits::SupportsPrerotate) { dim.Transform = _backbufferDimensions.Transform; }
	if (dim.Format == vk::Format::eUndefined) { dim.Format = _backbufferDimensions.Format; }
	if (resource.GetTransientState()) { dim.Flags |= AttachmentInfoFlagBits::InternalTransient; }

	switch (info.SizeClass) {
		case SizeClass::SwapchainRelative:
			dim.Width  = std::max(uint32_t(glm::ceil(info.Width * _backbufferDimensions.Width)), 1u);
			dim.Height = std::max(uint32_t(glm::ceil(info.Height * _backbufferDimensions.Height)), 1u);
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
	dim.MipLevels        = std::min(mipLevels, info.MipLevels == 0 ? ~0u : info.MipLevels);

	return dim;
}

RenderTextureResource& RenderGraph::GetTextureResource(const std::string& name) {
	auto it = _resourceToIndex.find(name);
	if (it != _resourceToIndex.end()) {
		Log::Assert(_resources[it->second]->GetType() == RenderResource::Type::Texture,
		            "RenderGraph",
		            "GetTextureResource() used to retrieve a non-texture resource.");

		return static_cast<RenderTextureResource&>(*_resources[it->second]);
	}

	uint32_t index = _resources.size();
	_resources.emplace_back(new RenderTextureResource(index));
	_resources.back()->SetName(name);
	_resourceToIndex[name] = index;

	return static_cast<RenderTextureResource&>(*_resources.back());
}

void RenderGraph::SetBackbufferDimensions(const ResourceDimensions& dimensions) {
	_backbufferDimensions = dimensions;
}

void RenderGraph::SetBackbufferSource(const std::string& source) {
	_backbufferSource = source;
}

std::vector<Vulkan::BufferHandle> RenderGraph::ConsumePhysicalBuffers() const {
	return _physicalBuffers;
}

void RenderGraph::InstallPhysicalBuffers(std::vector<Vulkan::BufferHandle>& buffers) {
	_physicalBuffers = std::move(buffers);
}

void RenderGraph::ValidatePasses() {
	for (auto& passPtr : _passes) {
		auto& pass = *passPtr;

		if (pass.GetBlitTextureInputs().size() != pass.GetBlitTextureOutputs().size()) {
			throw std::logic_error("[RenderGraph] Every blit texture output must have a blit texture input.");
		}
		if (pass.GetColorInputs().size() != pass.GetColorOutputs().size()) {
			throw std::logic_error("[RenderGraph] Every color output must have a color input.");
		}
		if (pass.GetStorageInputs().size() != pass.GetStorageOutputs().size()) {
			throw std::logic_error("[RenderGraph] Every storage buffer output must have a storage buffer input.");
		}
		if (pass.GetStorageTextureInputs().size() != pass.GetStorageTextureOutputs().size()) {
			throw std::logic_error("[RenderGraph] Every storage texture output must have a storage texture input.");
		}
		if (!pass.GetResolveOutputs().empty() && pass.GetResolveOutputs().size() != pass.GetColorOutputs().size()) {
			throw std::logic_error("[RenderGraph] Every resolve output must have a color output.");
		}

		// For each color output, if the input is not the same size, ensure it is added to the scaled input list.
		const uint32_t inputCount = pass.GetColorInputs().size();
		for (uint32_t i = 0; i < inputCount; ++i) {
			if (!pass.GetColorInputs()[i]) { continue; }

			if (GetResourceDimensions(*pass.GetColorInputs()[i]) != GetResourceDimensions(*pass.GetColorOutputs()[i])) {
				pass.MakeColorInputScaled(i);
			}
		}

		// Ensure the input and output buffers in Read-Modify-Write operations are the same size and usage.
		if (!pass.GetStorageOutputs().empty()) {
			const uint32_t outputCount = pass.GetStorageOutputs().size();
			for (uint32_t i = 0; i < outputCount; ++i) {
				if (!pass.GetStorageInputs()[i]) { continue; }

				if (pass.GetStorageOutputs()[i]->GetBufferInfo() != pass.GetStorageInputs()[i]->GetBufferInfo()) {
					throw std::logic_error("[RenderGraph] Performing Read-Modify-Write operations on incompatible buffers.");
				}
			}
		}

		// Ensure the input and output textures for blit operations are the same size and usage.
		if (!pass.GetBlitTextureOutputs().empty()) {
			const uint32_t outputCount = pass.GetBlitTextureOutputs().size();
			for (uint32_t i = 0; i < outputCount; ++i) {
				if (!pass.GetBlitTextureInputs()[i]) { continue; }

				if (GetResourceDimensions(*pass.GetBlitTextureOutputs()[i]) !=
				    GetResourceDimensions(*pass.GetBlitTextureInputs()[i])) {
					throw std::logic_error("[RenderGraph] Performing blit operations on incompatible textures.");
				}
			}
		}

		// Ensure the input and output textures in Read-Modify-Write operations are the same size and usage.
		if (!pass.GetStorageTextureOutputs().empty()) {
			const uint32_t outputCount = pass.GetStorageTextureOutputs().size();
			for (uint32_t i = 0; i < outputCount; ++i) {
				if (!pass.GetStorageTextureInputs()[i]) { continue; }

				if (GetResourceDimensions(*pass.GetStorageTextureOutputs()[i]) !=
				    GetResourceDimensions(*pass.GetStorageTextureInputs()[i])) {
					throw std::logic_error("[RenderGraph] Performing Read-Modify-Write operations on incompatible textures.");
				}
			}
		}

		// Ensure the input and output textures for depth/stencil operations are the same size and usage.
		if (pass.GetDepthStencilInput() && pass.GetDepthStencilOutput()) {
			if (GetResourceDimensions(*pass.GetDepthStencilInput()) != GetResourceDimensions(*pass.GetDepthStencilOutput())) {
				throw std::logic_error("[RenderGraph] Incompatible depth/stencil input and outputs.");
			}
		}
	}
}

void RenderGraph::TraverseDependencies(const RenderPass& pass, uint32_t depth) {
	// This function and DependPassesRecursive() work hand-in-hand to walk the render graph and determine all of the
	// render passes to add to the graph to create a specific attachment.
	// While we walk the graph, we make sure to look out for any opportunities to merge multiple logical render passes
	// into a single physical render pass.

	// We check the Depth/Stencil input first, because they are the most important for determining if the render
	// pass can be merged.
	if (pass.GetDepthStencilInput()) {
		// Require Resource Write, Disallow Render Pass Self-Dependency, Allow Dependency Merging
		DependPassesRecursive(pass, pass.GetDepthStencilInput()->GetWritePasses(), depth, false, false, true);
	}

	for (auto* input : pass.GetAttachmentInputs()) {
		const auto& colorOutputs = pass.GetColorOutputs();
		bool selfDependency      = pass.GetDepthStencilOutput() == input;
		if (std::find(colorOutputs.begin(), colorOutputs.end(), input) != colorOutputs.end()) { selfDependency = true; }

		if (!selfDependency) {
			// Require Resource Write, Disallow Render Pass Self-Dependency, Allow Dependency Merging
			DependPassesRecursive(pass, input->GetWritePasses(), depth, false, false, true);
		}
	}

	// Require Resource Write, Disallow Render Pass Self-Dependency, Allow Dependency Merging
	for (auto* input : pass.GetColorInputs()) {
		if (input) { DependPassesRecursive(pass, input->GetWritePasses(), depth, false, false, true); }
	}
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

void RenderGraph::DependPassesRecursive(const RenderPass& self,
                                        const std::unordered_set<uint32_t>& passes,
                                        uint32_t depth,
                                        bool noCheck,
                                        bool ignoreSelf,
                                        bool mergeDependency) {
	// Ensure the resource is written to, unless otherwise ignored.
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

	++depth;

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

void RenderGraph::ReorderPasses() {}

void RenderGraph::BuildPhysicalResources() {
	uint32_t physicalIndex;

	for (const auto passIndex : _passStack) {
		auto& pass = *_passes[passIndex];

		const auto& AddOrMergeBuffer = [&](RenderBufferResource* buffer, RenderBufferResource* mergeCandidate = nullptr) {
			if (!buffer) { return; }

			if (buffer->GetPhysicalIndex() == RenderResource::Unused) {
				_physicalDimensions.push_back(GetResourceDimensions(*buffer));
				buffer->SetPhysicalIndex(physicalIndex++);

				_physicalNames.push_back(buffer->GetName());
			} else {
				_physicalDimensions[buffer->GetPhysicalIndex()].Queues |= buffer->GetUsedQueues();
				_physicalDimensions[buffer->GetPhysicalIndex()].BufferInfo.Usage |= buffer->GetBufferUsage();

				auto& name = _physicalNames[buffer->GetPhysicalIndex()];
				name       = std::format("{}/{}", name, buffer->GetName());
			}

			if (mergeCandidate) {
				if (mergeCandidate->GetPhysicalIndex() == RenderResource::Unused) {
					mergeCandidate->SetPhysicalIndex(buffer->GetPhysicalIndex());
				} else if (mergeCandidate->GetPhysicalIndex() != buffer->GetPhysicalIndex()) {
					throw std::logic_error("[RenderGraph] Cannot alias resources, index already claimed.");
				}
			}
		};
		const auto& AddOrMergeTexture = [&](RenderTextureResource* texture,
		                                    RenderTextureResource* mergeCandidate = nullptr,
		                                    vk::ImageUsageFlags additionalUsage   = {},
		                                    bool depthStencil                     = false) {
			if (!texture) { return; }

			if (texture->GetPhysicalIndex() == RenderResource::Unused) {
				_physicalDimensions.push_back(GetResourceDimensions(*texture));
				texture->SetPhysicalIndex(physicalIndex++);
				_physicalDimensions[texture->GetPhysicalIndex()].ImageUsage |= additionalUsage;

				_physicalNames.push_back(texture->GetName());
			} else {
				_physicalDimensions[texture->GetPhysicalIndex()].Queues |= texture->GetUsedQueues();
				_physicalDimensions[texture->GetPhysicalIndex()].ImageUsage |= texture->GetImageUsage() | additionalUsage;

				auto& name = _physicalNames[texture->GetPhysicalIndex()];
				name       = std::format("{}/{}", name, texture->GetName());
			}

			if (mergeCandidate) {
				if (mergeCandidate->GetPhysicalIndex() == RenderResource::Unused) {
					mergeCandidate->SetPhysicalIndex(texture->GetPhysicalIndex());
				} else if (mergeCandidate->GetPhysicalIndex() != texture->GetPhysicalIndex()) {
					throw std::logic_error("[RenderGraph] Cannot alias resources, index already claimed.");
				}

				if (depthStencil) {
					_physicalDimensions[mergeCandidate->GetPhysicalIndex()].Queues |= mergeCandidate->GetUsedQueues();
					_physicalDimensions[mergeCandidate->GetPhysicalIndex()].ImageUsage |= mergeCandidate->GetImageUsage();
				}
			}
		};
		const auto& AddOrMergeProxy = [&](RenderResource* proxy) {
			if (!proxy) { return; }

			if (proxy->GetPhysicalIndex() == RenderResource::Unused) {
				_physicalDimensions.push_back(ResourceDimensions{.Flags = AttachmentInfoFlagBits::InternalProxy});
				proxy->SetPhysicalIndex(physicalIndex++);
			} else {
				_physicalDimensions[proxy->GetPhysicalIndex()].Queues |= proxy->GetUsedQueues();
			}
		};

		// Here, we go through each of the input resources used for a render pass and assign them a physical index.
		// If a resource already has a physical index, we add to its queue flags and usage information.
		// If possible, we try to use 1 physical resource for both input and output, since there's no point in having them
		// be different.

		// We start with the generic inputs.
		for (auto& input : pass.GetGenericBufferInputs()) { AddOrMergeBuffer(input.Buffer); }
		for (auto& input : pass.GetGenericTextureInputs()) { AddOrMergeTexture(input.Texture); }

		// Then we handle the color scaling inputs before the other color inputs.
		for (auto* input : pass.GetColorScaleInputs()) {
			AddOrMergeTexture(input, nullptr, vk::ImageUsageFlagBits::eSampled);
		}

		// Now we handle the input resources that may be able to alias with their output resource.
		if (!pass.GetColorInputs().empty()) {
			const uint32_t count = pass.GetColorInputs().size();
			for (uint32_t i = 0; i < count; ++i) { AddOrMergeTexture(pass.GetColorInputs()[i], pass.GetColorOutputs()[i]); }
		}
		if (!pass.GetStorageInputs().empty()) {
			const uint32_t count = pass.GetStorageInputs().size();
			for (uint32_t i = 0; i < count; ++i) {
				AddOrMergeBuffer(pass.GetStorageInputs()[i], pass.GetStorageOutputs()[i]);
			}
		}
		if (!pass.GetBlitTextureInputs().empty()) {
			const uint32_t count = pass.GetBlitTextureInputs().size();
			for (uint32_t i = 0; i < count; ++i) {
				AddOrMergeTexture(pass.GetBlitTextureInputs()[i], pass.GetBlitTextureOutputs()[i]);
			}
		}
		if (!pass.GetStorageTextureInputs().empty()) {
			const uint32_t count = pass.GetStorageTextureInputs().size();
			for (uint32_t i = 0; i < count; ++i) {
				AddOrMergeTexture(pass.GetStorageTextureInputs()[i], pass.GetStorageTextureOutputs()[i]);
			}
		}

		// Finally, add the proxy inputs.
		for (auto& input : pass.GetProxyInputs()) { AddOrMergeProxy(input.Proxy); }

		// Now we add the outputs, if they weren't able to be aliased by one of the inputs.
		for (auto* output : pass.GetColorOutputs()) { AddOrMergeTexture(output); }
		for (auto* output : pass.GetResolveOutputs()) { AddOrMergeTexture(output); }
		for (auto* output : pass.GetStorageOutputs()) { AddOrMergeBuffer(output); }
		for (auto& output : pass.GetProxyOutputs()) { AddOrMergeProxy(output.Proxy); }
		for (auto* output : pass.GetTransferOutputs()) { AddOrMergeBuffer(output); }
		for (auto* output : pass.GetBlitTextureOutputs()) { AddOrMergeTexture(output); }
		for (auto* output : pass.GetStorageTextureOutputs()) { AddOrMergeTexture(output); }

		// Any time we have a depth/stencil input and output, it should always be aliased.
		if (pass.GetDepthStencilInput()) {
			AddOrMergeTexture(pass.GetDepthStencilInput(), pass.GetDepthStencilOutput(), {}, true);
		} else if (pass.GetDepthStencilOutput()) {
			AddOrMergeTexture(pass.GetDepthStencilOutput());
		}

		// We handle the attachment inputs after all of the others to ensure they can alias with other color or
		// depth/stencil resources when possible.
		for (auto* input : pass.GetAttachmentInputs()) { AddOrMergeTexture(input); }

		for (auto& pair : pass.GetFakeResourceAliases()) { pair.second->SetPhysicalIndex(pair.first->GetPhysicalIndex()); }
	}

	// Now that we have all of our physical indices, we need to determine which physical images are used for history
	// input.
	_physicalImageHasHistory.clear();
	_physicalImageHasHistory.resize(_physicalDimensions.size());
	for (auto passIndex : _passStack) {
		auto& pass = *_passes[passIndex];
		for (auto& history : pass.GetHistoryInputs()) {
			if (history->GetPhysicalIndex() == RenderResource::Unused) {
				throw std::logic_error("[RenderGraph] History input is used, but never written to.");
			}
			_physicalImageHasHistory[history->GetPhysicalIndex()] = true;
		}
	}
}

void RenderGraph::BuildPhysicalPasses() {
	_physicalPasses.clear();

	PhysicalPass physicalPass;

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
	const auto DifferentAttachment = [](const RenderResource* a, const RenderResource* b) -> bool {
		return a && b && a->GetPhysicalIndex() != b->GetPhysicalIndex();
	};
	const auto SameAttachment = [](const RenderResource* a, const RenderResource* b) -> bool {
		return a && b && a->GetPhysicalIndex() == b->GetPhysicalIndex();
	};

	// The all-important function that determines whether we should merge two logical render passes into one physical
	// render pass.
	const auto ShouldMerge = [&](const RenderPass& prev, const RenderPass& next) -> bool {
		// We can only merge render passes which are Graphics, and within the same Queue.
		if ((prev.GetQueue() & ComputeQueues) || (prev.GetQueue() != next.GetQueue())) { return false; }

		// If we need to generate mipmaps after this pass, we cannot merge.
		for (auto* output : prev.GetColorOutputs()) {
			const auto& dim = _physicalDimensions[output->GetPhysicalIndex()];
			if (dim.MipLevels > 1 && dim.Flags & AttachmentInfoFlagBits::GenerateMips) { return false; }
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

		// If any of the depth attachments differ, we cannot merge.
		if (DifferentAttachment(next.GetDepthStencilInput(), prev.GetDepthStencilInput())) { return false; }
		if (DifferentAttachment(next.GetDepthStencilInput(), prev.GetDepthStencilOutput())) { return false; }
		if (DifferentAttachment(next.GetDepthStencilOutput(), prev.GetDepthStencilInput())) { return false; }
		if (DifferentAttachment(next.GetDepthStencilOutput(), prev.GetDepthStencilOutput())) { return false; }

		// We have ruled out all of the reasons why we cannot merge, now we work to determine if we SHOULD merge.

		// If the previous render pass writes to a color or resolve output that we use as color input, it's the perfect time
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

		// If the previous render pass writes to a color, resolve, or depth/stencil output that we use as an attachment
		// input, it's the perfect time for a subpass.
		for (auto* input : next.GetAttachmentInputs()) {
			if (FindAttachment(prev.GetColorOutputs(), input)) { return true; }
			if (FindAttachment(prev.GetResolveOutputs(), input)) { return true; }
			if (input && prev.GetDepthStencilOutput() == input) { return true; }
		}

		// At this point, we have determined that we technically can merge these passes together, but we have no good reason
		// to do so.
		return false;
	};

	// Here, we start at the beginning and try to greedily merge as many logical render passes as we can into physical
	// passes.
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

		physicalPass.Passes.insert(physicalPass.Passes.end(), _passStack.begin() + index, _passStack.begin() + mergeEnd);
		_physicalPasses.push_back(std::move(physicalPass));
		index = mergeEnd;
	}

	// We have all of our physical passes, now we tell the logical passes which physical pass they belong to.
	for (uint32_t i = 0; i < _physicalPasses.size(); ++i) {
		auto& physicalPass = _physicalPasses[i];
		for (auto& pass : physicalPass.Passes) { _passes[pass]->SetPhysicalPassIndex(i); }
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

		// History images can also never be transient, by their very nature.
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
		 * true; if it has been used already, the second output will be false.
		 */
		auto AddUniqueColor = [&](uint32_t index) -> std::pair<uint32_t, bool> {
			auto it = std::find(colors.begin(), colors.end(), index);
			if (it != colors.end()) { return {uint32_t(it - colors.begin()), false}; }

			uint32_t ret = colors.size();
			colors.push_back(index);

			return {ret, true};
		};
		auto AddUniqueInputAttachment = [&](uint32_t index) -> std::pair<uint32_t, bool> {
			if (index == physicalPass.PhysicalDepthStencilAttachment) {
				return {uint32_t(colors.size()), false};
			} else {
				return AddUniqueColor(index);
			}
		};
		auto AddUniqueDS = [&](uint32_t index) -> std::pair<uint32_t, bool> {
			bool newAttachment = physicalPass.PhysicalDepthStencilAttachment == RenderResource::Unused;
			physicalPass.PhysicalDepthStencilAttachment = index;

			return {index, newAttachment};
		};
		auto CheckPreserve = [this, physicalPassIndex](const RenderResource& tex) -> bool {
			for (auto& readPass : tex.GetReadPasses()) {
				if (_passes[readPass]->GetPhysicalPassIndex() > physicalPassIndex) { return true; }
			}

			return false;
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
						// We have no input, so the operation is either don't care or clear.
						if (pass.GetClearColor(i)) {
							rp.ClearAttachmentMask |= 1u << res.first;
							physicalPass.ColorClearRequests.push_back({&pass, &rp.ClearColors[res.first], i});
						}
					} else {
						// We have a color input, so the operation is either load, or we need to add a scaled clear.
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

			// Add our depth/stencil input and output.
			auto* dsInput  = pass.GetDepthStencilInput();
			auto* dsOutput = pass.GetDepthStencilOutput();
			if (dsInput && dsOutput) {
				auto res = AddUniqueDS(dsOutput->GetPhysicalIndex());

				// If this is the first use, we must load.
				if (res.second) { rp.LoadAttachmentMask |= 1u << res.first; }

				rp.Flags |= Vulkan::RenderPassFlagBits::StoreDepthStencil;
				rp.Subpasses[subpassIndex].DepthStencil = Vulkan::DepthStencilUsage::ReadWrite;
			} else if (dsOutput) {
				auto res = AddUniqueDS(dsOutput->GetPhysicalIndex());

				// If this is the first use and we have a clear value, claer it.
				if (res.second && pass.GetClearDepthStencil()) {
					rp.Flags |= Vulkan::RenderPassFlagBits::ClearDepthStencil;
					physicalPass.DepthClearRequest.Pass   = &pass;
					physicalPass.DepthClearRequest.Target = &rp.ClearDepthStencil;
				}

				rp.Flags |= Vulkan::RenderPassFlagBits::StoreDepthStencil;
				rp.Subpasses[subpassIndex].DepthStencil     = Vulkan::DepthStencilUsage::ReadWrite;
				physicalPass.PhysicalDepthStencilAttachment = dsOutput->GetPhysicalIndex();
			} else if (dsInput) {
				auto res = AddUniqueDS(dsInput->GetPhysicalIndex());

				if (res.second) {
					// If this is the first use, we need to load.
					rp.Flags |= Vulkan::RenderPassFlagBits::DepthStencilReadOnly | Vulkan::RenderPassFlagBits::LoadDepthStencil;

					// We also need to see if this needs to be stored again, for use in a later pass.
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
					if (preserveDepth) { rp.Flags |= Vulkan::RenderPassFlagBits::StoreDepthStencil; }
				}

				rp.Subpasses[subpassIndex].DepthStencil = Vulkan::DepthStencilUsage::ReadOnly;
			} else {
				rp.Subpasses[subpassIndex].DepthStencil = Vulkan::DepthStencilUsage::None;
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

void RenderGraph::BuildBarriers() {
	// Here we handle the memory barriers and dependencies to keep our graph executing properly.
	// Each resource may need a flush barrier, an invalidate barrier, or both.
	// An Invalidate barrier is used for inputs, to invalidate the cache and ensure all pending writes have finished
	// before we read or write it.
	// A Flush barrier is used for outputs, to flush the cache and ensure the new data is visible to any future reads.

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

	for (auto passIndex : _passStack) {
		auto& pass        = *_passes[passIndex];
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
		for (auto& input : pass.GetGenericTextureInputs()) {
			auto& barrier = GetInvalidateAccess(input.Texture->GetPhysicalIndex(), false);
			barrier.Access |= input.Access;
			barrier.Stages |= input.Stages;
			if (barrier.Layout != vk::ImageLayout::eUndefined) { throw std::logic_error("[RenderGraph] Layout mismatch."); }
			barrier.Layout = input.Layout;
		}
		for (auto* input : pass.GetAttachmentInputs()) {
			if (pass.GetQueue() & ComputeQueues) {
				throw std::logic_error("[RenderGraph] Only graphics passes can have input attachments.");
			}

			auto& barrier = GetInvalidateAccess(input->GetPhysicalIndex(), false);
			barrier.Access |= vk::AccessFlagBits2::eInputAttachmentRead;
			barrier.Stages |= vk::PipelineStageFlagBits2::eFragmentShader;

			if (Vulkan::FormatHasDepthOrStencil(input->GetAttachmentInfo().Format)) {
				barrier.Access |= vk::AccessFlagBits2::eDepthStencilAttachmentRead;
				barrier.Stages |=
					vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
			} else {
				barrier.Access |= vk::AccessFlagBits2::eColorAttachmentRead;
				barrier.Stages |= vk::PipelineStageFlagBits2::eColorAttachmentOutput;
			}

			if (barrier.Layout != vk::ImageLayout::eUndefined) { throw std::logic_error("[RenderGraph] Layout mismatch."); }
			barrier.Layout = vk::ImageLayout::eShaderReadOnlyOptimal;
		}
		for (auto* input : pass.GetBlitTextureInputs()) {
			if (!input) { continue; }

			auto& barrier = GetInvalidateAccess(input->GetPhysicalIndex(), false);
			barrier.Access |= vk::AccessFlagBits2::eTransferRead;
			barrier.Stages |= vk::PipelineStageFlagBits2::eBlit;

			if (barrier.Layout != vk::ImageLayout::eUndefined) { throw std::logic_error("[RenderGraph] Layout mismatch."); }
			barrier.Layout = vk::ImageLayout::eTransferSrcOptimal;
		}
		for (auto* input : pass.GetColorInputs()) {
			if (!input) { continue; }

			if (pass.GetQueue() & ComputeQueues) {
				throw std::logic_error("[RenderGraph] Only graphics passes can have input attachments.");
			}

			auto& barrier = GetInvalidateAccess(input->GetPhysicalIndex(), false);
			barrier.Access |= vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite;
			barrier.Stages |= vk::PipelineStageFlagBits2::eColorAttachmentOutput;

			if (barrier.Layout == vk::ImageLayout::eShaderReadOnlyOptimal) {
				barrier.Layout = vk::ImageLayout::eGeneral;
			} else if (barrier.Layout != vk::ImageLayout::eUndefined) {
				throw std::logic_error("[RenderGraph] Layout mismatch.");
			} else {
				barrier.Layout = vk::ImageLayout::eColorAttachmentOptimal;
			}
		}
		for (auto* input : pass.GetColorScaleInputs()) {
			if (!input) { continue; }

			if (pass.GetQueue() & ComputeQueues) {
				throw std::logic_error("[RenderGraph] Only graphics passes can have input attachments.");
			}

			auto& barrier = GetInvalidateAccess(input->GetPhysicalIndex(), false);
			barrier.Access |= vk::AccessFlagBits2::eShaderSampledRead;
			barrier.Stages |= vk::PipelineStageFlagBits2::eFragmentShader;

			if (barrier.Layout != vk::ImageLayout::eUndefined) { throw std::logic_error("[RenderGraph] Layout mismatch."); }
			barrier.Layout = vk::ImageLayout::eShaderReadOnlyOptimal;
		}
		for (auto* input : pass.GetHistoryInputs()) {
			auto& barrier = GetInvalidateAccess(input->GetPhysicalIndex(), true);
			barrier.Access |= vk::AccessFlagBits2::eShaderSampledRead;
			if (pass.GetQueue() & ComputeQueues) {
				barrier.Stages |= vk::PipelineStageFlagBits2::eComputeShader;
			} else {
				barrier.Stages |= vk::PipelineStageFlagBits2::eFragmentShader;
			}

			if (barrier.Layout != vk::ImageLayout::eUndefined) { throw std::logic_error("[RenderGraph] Layout mismatch."); }
			barrier.Layout = vk::ImageLayout::eShaderReadOnlyOptimal;
		}
		for (auto& input : pass.GetProxyInputs()) {
			auto& barrier = GetInvalidateAccess(input.Proxy->GetPhysicalIndex(), false);
			barrier.Stages |= input.Stages;

			if (barrier.Layout != vk::ImageLayout::eUndefined) { throw std::logic_error("[RenderGraph] Layout mismatch."); }
			barrier.Layout = input.Layout;
		}
		for (auto* input : pass.GetStorageInputs()) {
			if (!input) { continue; }

			auto& barrier = GetInvalidateAccess(input->GetPhysicalIndex(), false);
			barrier.Access |= vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite;
			if (pass.GetQueue() & ComputeQueues) {
				barrier.Stages |= vk::PipelineStageFlagBits2::eComputeShader;
			} else {
				barrier.Stages |= vk::PipelineStageFlagBits2::eFragmentShader;
			}

			if (barrier.Layout != vk::ImageLayout::eUndefined) { throw std::logic_error("[RenderGraph] Layout mismatch."); }
			barrier.Layout = vk::ImageLayout::eGeneral;
		}
		for (auto* input : pass.GetStorageTextureInputs()) {
			if (!input) { continue; }

			auto& barrier = GetInvalidateAccess(input->GetPhysicalIndex(), false);
			barrier.Access |= vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite;
			if (pass.GetQueue() & ComputeQueues) {
				barrier.Stages |= vk::PipelineStageFlagBits2::eComputeShader;
			} else {
				barrier.Stages |= vk::PipelineStageFlagBits2::eFragmentShader;
			}

			if (barrier.Layout != vk::ImageLayout::eUndefined) { throw std::logic_error("[RenderGraph] Layout mismatch."); }
			barrier.Layout = vk::ImageLayout::eGeneral;
		}

		// Handle all of our outputs (flush barriers).
		for (auto* output : pass.GetBlitTextureOutputs()) {
			auto& barrier = GetFlushAccess(output->GetPhysicalIndex());
			barrier.Access |= vk::AccessFlagBits2::eTransferWrite;
			barrier.Stages |= vk::PipelineStageFlagBits2::eBlit;

			if (barrier.Layout != vk::ImageLayout::eUndefined) { throw std::logic_error("[RenderGraph] Layout mismatch."); }
			barrier.Layout = vk::ImageLayout::eTransferDstOptimal;
		}
		for (auto* output : pass.GetColorOutputs()) {
			if (pass.GetQueue() & ComputeQueues) {
				throw std::logic_error("[RenderGraph] Only graphics passes can have color outputs.");
			}

			auto& barrier = GetFlushAccess(output->GetPhysicalIndex());
			if ((_physicalDimensions[output->GetPhysicalIndex()].MipLevels > 1) &&
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
		for (auto& output : pass.GetProxyOutputs()) {
			auto& barrier = GetFlushAccess(output.Proxy->GetPhysicalIndex());
			barrier.Stages |= output.Stages;

			if (barrier.Layout != vk::ImageLayout::eUndefined) { throw std::logic_error("[RenderGraph] Layout mismatch."); }
			barrier.Layout = output.Layout;
		}
		for (auto* output : pass.GetResolveOutputs()) {
			if (pass.GetQueue() & ComputeQueues) {
				throw std::logic_error("[RenderGraph] Only graphics passes can have resolve outputs.");
			}

			auto& barrier = GetFlushAccess(output->GetPhysicalIndex());
			barrier.Access |= vk::AccessFlagBits2::eColorAttachmentWrite;
			barrier.Stages |= vk::PipelineStageFlagBits2::eColorAttachmentOutput;

			if (barrier.Layout != vk::ImageLayout::eUndefined) { throw std::logic_error("[RenderGraph] Layout mismatch."); }
			barrier.Layout = vk::ImageLayout::eColorAttachmentOptimal;
		}
		for (auto* output : pass.GetStorageOutputs()) {
			auto& barrier = GetFlushAccess(output->GetPhysicalIndex());
			barrier.Access |= vk::AccessFlagBits2::eShaderStorageWrite;
			if (pass.GetQueue() & ComputeQueues) {
				barrier.Stages |= vk::PipelineStageFlagBits2::eComputeShader;
			} else {
				barrier.Stages |= vk::PipelineStageFlagBits2::eFragmentShader;
			}

			if (barrier.Layout != vk::ImageLayout::eUndefined) { throw std::logic_error("[RenderGraph] Layout mismatch."); }
			barrier.Layout = vk::ImageLayout::eGeneral;
		}
		for (auto* output : pass.GetStorageTextureOutputs()) {
			auto& barrier = GetFlushAccess(output->GetPhysicalIndex());
			barrier.Access |= vk::AccessFlagBits2::eShaderStorageWrite;
			if (pass.GetQueue() & ComputeQueues) {
				barrier.Stages |= vk::PipelineStageFlagBits2::eComputeShader;
			} else {
				barrier.Stages |= vk::PipelineStageFlagBits2::eFragmentShader;
			}

			if (barrier.Layout != vk::ImageLayout::eUndefined) { throw std::logic_error("[RenderGraph] Layout mismatch."); }
			barrier.Layout = vk::ImageLayout::eGeneral;
		}
		for (auto* output : pass.GetTransferOutputs()) {
			auto& barrier = GetFlushAccess(output->GetPhysicalIndex());
			barrier.Access |= vk::AccessFlagBits2::eTransferWrite;
			barrier.Stages |= vk::PipelineStageFlagBits2::eCopy | vk::PipelineStageFlagBits2::eClear;

			if (barrier.Layout != vk::ImageLayout::eUndefined) { throw std::logic_error("[RenderGraph] Layout mismatch."); }
			barrier.Layout = vk::ImageLayout::eGeneral;
		}

		// Handle our depth/stencil attachments.
		auto* dsInput  = pass.GetDepthStencilInput();
		auto* dsOutput = pass.GetDepthStencilOutput();
		if ((dsInput || dsOutput) && (pass.GetQueue() & ComputeQueues)) {
			throw std::logic_error("[RenderGraph] Only graphics passes can have depth attachments.");
		}
		if (dsInput && dsOutput) {
			auto& inBarrier  = GetInvalidateAccess(dsInput->GetPhysicalIndex(), false);
			auto& outBarrier = GetFlushAccess(dsOutput->GetPhysicalIndex());

			if (inBarrier.Layout == vk::ImageLayout::eShaderReadOnlyOptimal) {
				inBarrier.Layout = vk::ImageLayout::eGeneral;
			} else if (inBarrier.Layout != vk::ImageLayout::eUndefined) {
				throw std::logic_error("[RenderGraph] Layout mismatch.");
			} else {
				inBarrier.Layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			}

			inBarrier.Access |=
				vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
			inBarrier.Stages |=
				vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;

			outBarrier.Layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			outBarrier.Access |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
			outBarrier.Stages |=
				vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
		} else if (dsInput) {
			auto& inBarrier = GetInvalidateAccess(dsInput->GetPhysicalIndex(), false);

			if (inBarrier.Layout == vk::ImageLayout::eShaderReadOnlyOptimal) {
				inBarrier.Layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			} else if (inBarrier.Layout != vk::ImageLayout::eUndefined) {
				throw std::logic_error("[RenderGraph] Layout mismatch.");
			} else {
				inBarrier.Layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			}

			inBarrier.Access |= vk::AccessFlagBits2::eDepthStencilAttachmentRead;
			inBarrier.Stages |=
				vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
		} else if (dsOutput) {
			auto& outBarrier = GetFlushAccess(dsOutput->GetPhysicalIndex());

			if (outBarrier.Layout == vk::ImageLayout::eShaderReadOnlyOptimal) {
				outBarrier.Layout = vk::ImageLayout::eGeneral;
			} else if (outBarrier.Layout != vk::ImageLayout::eUndefined) {
				throw std::logic_error("[RenderGraph] Layout mismatch.");
			} else {
				outBarrier.Layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			}

			outBarrier.Access |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
			outBarrier.Stages |=
				vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
		}

		_passBarriers.push_back(std::move(barriers));
	}
}

void RenderGraph::BuildPhysicalBarriers() {
	auto barrierIt = _passBarriers.begin();

	const auto FlushAccessToInvalidate = [](vk::AccessFlags2 flags) -> vk::AccessFlags2 {
		if (flags & vk::AccessFlagBits2::eColorAttachmentWrite) { flags |= vk::AccessFlagBits2::eColorAttachmentRead; }
		if (flags & vk::AccessFlagBits2::eDepthStencilAttachmentWrite) {
			flags |= vk::AccessFlagBits2::eDepthStencilAttachmentRead;
		}
		if (flags & vk::AccessFlagBits2::eShaderWrite) { flags |= vk::AccessFlagBits2::eShaderRead; }
		if (flags & vk::AccessFlagBits2::eShaderStorageWrite) { flags |= vk::AccessFlagBits2::eShaderStorageRead; }

		return flags;
	};
	const auto FlushStageToInvalidate = [](vk::PipelineStageFlags2 flags) -> vk::PipelineStageFlags2 {
		if (flags & vk::PipelineStageFlagBits2::eLateFragmentTests) {
			flags |= vk::PipelineStageFlagBits2::eEarlyFragmentTests;
		}

		return flags;
	};

	struct ResourceState {
		vk::ImageLayout InitialLayout             = vk::ImageLayout::eUndefined;
		vk::ImageLayout FinalLayout               = vk::ImageLayout::eUndefined;
		vk::AccessFlags2 InvalidatedAccess        = {};
		vk::AccessFlags2 FlushedAccess            = {};
		vk::PipelineStageFlags2 InvalidatedStages = {};
		vk::PipelineStageFlags2 FlushedStages     = {};
	};
	std::vector<ResourceState> resourceStates(_physicalDimensions.size());

	for (auto& physicalPass : _physicalPasses) {
		std::fill(resourceStates.begin(), resourceStates.end(), ResourceState{});

		for (uint32_t i = 0; i < physicalPass.Passes.size(); ++i, ++barrierIt) {
			auto& barriers    = *barrierIt;
			auto& invalidates = barriers.Invalidate;
			auto& flushes     = barriers.Flush;

			for (auto& invalidate : invalidates) {
				auto& res       = resourceStates[invalidate.ResourceIndex];
				const auto& dim = _physicalDimensions[invalidate.ResourceIndex];

				if (dim.Flags & AttachmentInfoFlagBits::InternalTransient ||
				    invalidate.ResourceIndex == _backbufferPhysicalIndex) {
					continue;
				}

				if (invalidate.History) {
					auto it =
						std::find_if(physicalPass.Invalidate.begin(), physicalPass.Invalidate.end(), [&](const Barrier& b) -> bool {
							return b.ResourceIndex == invalidate.ResourceIndex && b.History;
						});
					if (it == physicalPass.Invalidate.end()) {
						auto layout = dim.IsStorageImage() ? vk::ImageLayout::eGeneral : invalidate.Layout;
						physicalPass.Invalidate.push_back(
							{invalidate.ResourceIndex, layout, invalidate.Access, invalidate.Stages, true});
						physicalPass.Flush.push_back({invalidate.ResourceIndex, layout, {}, invalidate.Stages, true});
					}

					continue;
				}

				if (res.InitialLayout == vk::ImageLayout::eUndefined) {
					res.InvalidatedAccess |= invalidate.Access;
					res.InvalidatedStages |= invalidate.Stages;

					if (dim.IsStorageImage()) {
						res.InitialLayout = vk::ImageLayout::eGeneral;
					} else {
						res.InitialLayout = invalidate.Layout;
					}
				}

				if (dim.IsStorageImage()) {
					res.FinalLayout = vk::ImageLayout::eGeneral;
				} else {
					res.FinalLayout = invalidate.Layout;
				}

				res.FlushedAccess = {};
				res.FlushedStages = {};
			}

			for (auto& flush : flushes) {
				auto& res       = resourceStates[flush.ResourceIndex];
				const auto& dim = _physicalDimensions[flush.ResourceIndex];

				if (dim.Flags & AttachmentInfoFlagBits::InternalTransient || flush.ResourceIndex == _backbufferPhysicalIndex) {
					continue;
				}

				res.FlushedAccess |= flush.Access;
				res.FlushedStages |= flush.Stages;

				if (dim.IsStorageImage()) {
					res.FinalLayout = vk::ImageLayout::eGeneral;
				} else {
					res.FinalLayout = flush.Layout;
				}

				if (res.InitialLayout == vk::ImageLayout::eUndefined) {
					if (flush.Layout == vk::ImageLayout::eTransferSrcOptimal) {
						res.InitialLayout = vk::ImageLayout::eColorAttachmentOptimal;
						res.InvalidatedAccess =
							vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite;
						res.InvalidatedStages = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
					} else {
						res.InitialLayout     = flush.Layout;
						res.InvalidatedAccess = FlushAccessToInvalidate(flush.Access);
						res.InvalidatedStages = FlushStageToInvalidate(flush.Stages);
					}

					physicalPass.Discards.push_back(flush.ResourceIndex);
				}
			}
		}

		for (uint32_t i = 0; i < resourceStates.size(); ++i) {
			const auto& resource = resourceStates[i];

			if (resource.InitialLayout == vk::ImageLayout::eUndefined &&
			    resource.FinalLayout == vk::ImageLayout::eUndefined) {
				continue;
			}

			physicalPass.Invalidate.push_back(
				{i, resource.InitialLayout, resource.InvalidatedAccess, resource.InvalidatedStages, false});

			if (resource.FlushedAccess) {
				physicalPass.Flush.push_back({i, resource.FinalLayout, resource.FlushedAccess, resource.FlushedStages, false});
			} else if (resource.InvalidatedAccess) {
				physicalPass.Flush.push_back({i, resource.FinalLayout, {}, resource.InvalidatedStages, false});
			}

			if (resource.FinalLayout == vk::ImageLayout::eTransferSrcOptimal) {
				physicalPass.MipmapRequests.push_back({i,
				                                       vk::PipelineStageFlagBits2::eColorAttachmentOutput,
				                                       vk::AccessFlagBits2::eColorAttachmentWrite,
				                                       vk::ImageLayout::eColorAttachmentOptimal});
			}
		}
	}
}

void RenderGraph::BuildAliases() {
	_physicalAliases.resize(_physicalDimensions.size());
	std::fill(_physicalAliases.begin(), _physicalAliases.end(), RenderResource::Unused);
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
	EnqueuePhysicalPassCPU(device, physicalPass, state, composer);
}

void RenderGraph::EnqueuePhysicalPassCPU(Vulkan::Device& device,
                                         const PhysicalPass& physicalPass,
                                         PassSubmissionState& state,
                                         TaskComposer& composer) {
	const bool physicalGraphics = device.GetQueueType(state.QueueType) == Vulkan::QueueType::Graphics;

	GetQueueType(state.QueueType, state.Graphics, _passes[physicalPass.Passes.front()]->GetQueue());

	PhysicalPassInvalidateAttachments(physicalPass);

	for (auto& barrier : physicalPass.Invalidate) { PhysicalPassInvalidateBarrier(barrier, state, physicalGraphics); }

	PhysicalPassSignal(device, physicalPass, state);
	for (auto& barrier : physicalPass.Flush) { PhysicalPassFlushBarrier(barrier, state); }

	PhysicalPassTransferOwnership(physicalPass);

	state.SubpassContents.resize(physicalPass.Passes.size());
	std::fill(state.SubpassContents.begin(), state.SubpassContents.end(), vk::SubpassContents::eInline);

	TaskComposer passComposer;
	passComposer.SetIncomingTask(composer.GetPipelineStageDependency());
	passComposer.BeginPipelineStage();
	for (auto passIndex : physicalPass.Passes) { _passes[passIndex]->PrepareRenderPass(passComposer); }
	state.RenderingDependency = passComposer.GetOutgoingTask();
}

void RenderGraph::EnqueuePhysicalPassGPU(Vulkan::Device& device,
                                         const PhysicalPass& physicalPass,
                                         PassSubmissionState& state) {
	auto group = Threading::CreateTaskGroup();
	group->Enqueue([&]() {
		state.Cmd = device.RequestCommandBuffer(state.QueueType);
		state.EmitPrePassBarriers();
		if (state.Graphics) {
			RecordGraphicsCommands(physicalPass, state);
		} else {
			RecordComputeCommands(physicalPass, state);
		}
		state.Cmd->EndThread();
	});

	if (state.RenderingDependency) { Threading::AddDependency(*group, *state.RenderingDependency); }
	state.RenderingDependency = group;
}

void RenderGraph::RecordComputeCommands(const PhysicalPass& physicalPass, PassSubmissionState& state) {}

void RenderGraph::RecordGraphicsCommands(const PhysicalPass& physicalPass, PassSubmissionState& state) {
	auto& cmd = *state.Cmd;

	for (auto& clearRequest : physicalPass.ColorClearRequests) {
		clearRequest.Pass->GetClearColor(clearRequest.Index, clearRequest.Target);
	}
	if (physicalPass.DepthClearRequest.Pass) {
		physicalPass.DepthClearRequest.Pass->GetClearDepthStencil(physicalPass.DepthClearRequest.Target);
	}

	auto rpInfo              = physicalPass.RenderPassInfo;
	uint32_t layerIterations = 1;
	if (physicalPass.ArrayLayers > 1) {
		// TODO
		Log::Warning("RenderGraph", "No support for multi-layered passes yet.");
		layerIterations = 0;
	}

	for (uint32_t layer = 0; layer < layerIterations; ++layer) {
		rpInfo.BaseLayer = layer;
		cmd.BeginRenderPass(rpInfo, state.SubpassContents[0]);

		for (uint32_t subpass = 0; subpass < physicalPass.Passes.size(); ++subpass) {
			uint32_t passIndex = physicalPass.Passes[subpass];
			auto& pass         = *_passes[passIndex];

			auto& scaledRequests = physicalPass.ScaledClearRequests[subpass];
			if (scaledRequests.size() > 0) {
				// TODO
				Log::Warning("RenderGraph", "No support for scaled clear requests.");
			}

			pass.BuildRenderPass(cmd, layer);

			if (subpass < (physicalPass.Passes.size() - 1)) { cmd.NextSubpass(state.SubpassContents[subpass + 1]); }
		}

		cmd.EndRenderPass();
	}

	if (physicalPass.MipmapRequests.size() > 0) {
		// TODO
		Log::Warning("RenderGraph", "No support for mipmap requests");
	}
}

void RenderGraph::SetupPhysicalBuffer(Vulkan::Device& device, uint32_t physicalIndex) {
	auto& att = _physicalDimensions[physicalIndex];

	Vulkan::BufferCreateInfo bufferCI(Vulkan::BufferDomain::Device, att.BufferInfo.Size, att.BufferInfo.Usage);
	bufferCI.Flags |= Vulkan::BufferCreateFlagBits::ZeroInitialize;

	// First check if we already have a suitable buffer.
	if (_physicalBuffers[physicalIndex]) {
		if (att.Flags & AttachmentInfoFlagBits::Persistent &&
		    _physicalBuffers[physicalIndex]->GetCreateInfo().Size == bufferCI.Size &&
		    (_physicalBuffers[physicalIndex]->GetCreateInfo().Usage & bufferCI.Usage) == bufferCI.Usage) {
			return;
		}
	}

	_physicalBuffers[physicalIndex] = device.CreateBuffer(bufferCI, nullptr, _physicalNames[physicalIndex]);
	_physicalEvents[physicalIndex]  = {};
}

void RenderGraph::SetupPhysicalImage(Vulkan::Device& device, uint32_t physicalIndex) {
	auto& att = _physicalDimensions[physicalIndex];

	if (_physicalAliases[physicalIndex] != RenderResource::Unused) {
		_physicalImages[physicalIndex]      = _physicalImages[_physicalAliases[physicalIndex]];
		_physicalAttachments[physicalIndex] = _physicalAttachments[_physicalAliases[physicalIndex]];
		_physicalEvents[physicalIndex]      = {};

		return;
	}

	bool needsImage                    = true;
	vk::ImageUsageFlags usage          = att.ImageUsage;
	Vulkan::ImageCreateFlags miscFlags = {};
	vk::ImageCreateFlags flags         = {};

	if (att.Flags & AttachmentInfoFlagBits::UnormSrgbAlias) { miscFlags |= Vulkan::ImageCreateFlagBits::MutableSrgb; }
	if (att.IsStorageImage()) { flags |= vk::ImageCreateFlagBits::eMutableFormat; }

	if (_physicalImages[physicalIndex]) {
		const auto& info = _physicalImages[physicalIndex]->GetCreateInfo();
		if (att.Flags & AttachmentInfoFlagBits::Persistent && info.Format == att.Format && info.Width == att.Width &&
		    info.Height == att.Height && info.Depth == att.Depth && (info.Usage & usage) == usage &&
		    (info.Flags & flags) == flags) {
			needsImage = false;
		}
	}

	if (needsImage) {
		Vulkan::ImageCreateInfo imageCI{.Domain        = Vulkan::ImageDomain::Physical,
		                                .Width         = att.Width,
		                                .Height        = att.Height,
		                                .Depth         = att.Depth,
		                                .MipLevels     = att.MipLevels,
		                                .ArrayLayers   = att.ArrayLayers,
		                                .Format        = att.Format,
		                                .InitialLayout = vk::ImageLayout::eUndefined,
		                                .Type          = att.Depth > 1 ? vk::ImageType::e3D : vk::ImageType::e2D,
		                                .Usage         = usage,
		                                .Samples       = vk::SampleCountFlagBits::e1,
		                                .Flags         = flags,
		                                .MiscFlags     = miscFlags,
		                                .Swizzle       = {}};
		if (Vulkan::FormatHasDepthOrStencil(imageCI.Format)) { imageCI.Usage &= ~vk::ImageUsageFlagBits::eColorAttachment; }

		if (att.Queues & (RenderGraphQueueFlagBits::Graphics | RenderGraphQueueFlagBits::Compute)) {
			imageCI.MiscFlags |= Vulkan::ImageCreateFlagBits::ConcurrentQueueGraphics;
		}
		if (att.Queues & RenderGraphQueueFlagBits::Compute) {
			imageCI.MiscFlags |= Vulkan::ImageCreateFlagBits::ConcurrentQueueAsyncCompute;
		}
		if (att.Queues & RenderGraphQueueFlagBits::AsyncGraphics) {
			imageCI.MiscFlags |= Vulkan::ImageCreateFlagBits::ConcurrentQueueAsyncGraphics;
		}

		_physicalImages[physicalIndex] = device.CreateImage(imageCI, nullptr, _physicalNames[physicalIndex]);
		_physicalEvents[physicalIndex] = {};
	}

	_physicalAttachments[physicalIndex] = &_physicalImages[physicalIndex]->GetView();
}

void RenderGraph::GetQueueType(Vulkan::CommandBufferType& cmdType,
                               bool& graphics,
                               RenderGraphQueueFlagBits flag) const {
	switch (flag) {
		default:
		case RenderGraphQueueFlagBits::Graphics:
			graphics = true;
			cmdType  = Vulkan::CommandBufferType::Generic;
			break;
		case RenderGraphQueueFlagBits::Compute:
			graphics = false;
			cmdType  = Vulkan::CommandBufferType::Generic;
			break;
		case RenderGraphQueueFlagBits::AsyncCompute:
			graphics = false;
			cmdType  = Vulkan::CommandBufferType::AsyncCompute;
			break;
		case RenderGraphQueueFlagBits::AsyncGraphics:
			graphics = true;
			cmdType  = Vulkan::CommandBufferType::AsyncGraphics;
			break;
	}
}

void RenderGraph::PhysicalPassFlushBarrier(const Barrier& barrier, PassSubmissionState& state) {
	auto& event =
		barrier.History ? _physicalHistoryEvents[barrier.ResourceIndex] : _physicalEvents[barrier.ResourceIndex];

	if (!_physicalDimensions[barrier.ResourceIndex].BufferInfo.Size) {
		auto* image = barrier.History ? _physicalHistoryImages[barrier.ResourceIndex].Get()
		                              : _physicalImages[barrier.ResourceIndex].Get();
		if (!image) { return; }

		_physicalEvents[barrier.ResourceIndex].Layout = barrier.Layout;
	}

	event.ToFlushAccess = barrier.Access;

	if (_physicalDimensions[barrier.ResourceIndex].UsesSemaphore()) {
		event.WaitGraphicsSemaphore    = state.ProxySemaphores[0];
		event.WaitComputeSemaphore     = state.ProxySemaphores[1];
		event.PipelineBarrierSrcStages = vk::PipelineStageFlagBits2::eNone;
	} else {
		event.PipelineBarrierSrcStages = barrier.Stages;
	}
}

void RenderGraph::PhysicalPassInvalidateAttachments(const PhysicalPass& physicalPass) {
	for (auto& discard : physicalPass.Discards) {
		if (!_physicalDimensions[discard].IsBufferLike()) { _physicalEvents[discard].Layout = vk::ImageLayout::eUndefined; }
	}
}

void RenderGraph::PhysicalPassInvalidateBarrier(const Barrier& barrier,
                                                PassSubmissionState& state,
                                                bool physicalGraphics) {
	auto& event =
		barrier.History ? _physicalHistoryEvents[barrier.ResourceIndex] : _physicalEvents[barrier.ResourceIndex];
	bool needsPipelineBarrier = false;
	bool layoutChange         = false;
	bool needsWaitSemaphore   = false;
	auto& waitSemaphore       = physicalGraphics ? event.WaitGraphicsSemaphore : event.WaitComputeSemaphore;

	bool needsInvalidate = false;
	ForEachBit64(uint64_t(barrier.Stages), [&](uint32_t bit) {
		if (barrier.Access & ~event.InvalidatedInStage[bit]) { needsInvalidate = true; }
	});

	auto& dim = _physicalDimensions[barrier.ResourceIndex];
	if (dim.BufferInfo.Size || dim.Flags & AttachmentInfoFlagBits::InternalProxy) {
		if (bool(event.ToFlushAccess) || needsInvalidate) {
			needsPipelineBarrier = bool(event.PipelineBarrierSrcStages);
			needsWaitSemaphore   = bool(waitSemaphore);
		}

		if (needsPipelineBarrier) {
			state.BufferBarriers.emplace_back(event.PipelineBarrierSrcStages,
			                                  event.ToFlushAccess,
			                                  barrier.Stages,
			                                  barrier.Access,
			                                  vk::QueueFamilyIgnored,
			                                  vk::QueueFamilyIgnored,
			                                  _physicalBuffers[barrier.ResourceIndex]->GetBuffer(),
			                                  0,
			                                  vk::WholeSize);
		}
	} else {
		const auto* image = barrier.History ? _physicalHistoryImages[barrier.ResourceIndex].Get()
		                                    : _physicalImages[barrier.ResourceIndex].Get();
		if (!image) { return; }

		vk::ImageMemoryBarrier2 imageBarrier(
			{},
			event.ToFlushAccess,
			barrier.Stages,
			barrier.Access,
			event.Layout,
			barrier.Layout,
			vk::QueueFamilyIgnored,
			vk::QueueFamilyIgnored,
			image->GetImage(),
			vk::ImageSubresourceRange(Vulkan::FormatAspectFlags(image->GetCreateInfo().Format),
		                            0,
		                            image->GetCreateInfo().MipLevels,
		                            0,
		                            image->GetCreateInfo().ArrayLayers));

		layoutChange = imageBarrier.oldLayout != imageBarrier.newLayout;

		if (layoutChange || bool(event.ToFlushAccess) || needsInvalidate) {
			if (event.PipelineBarrierSrcStages) {
				imageBarrier.srcStageMask = event.PipelineBarrierSrcStages;
				state.ImageBarriers.push_back(imageBarrier);
				needsPipelineBarrier = true;
			} else if (waitSemaphore) {
				if (layoutChange) {
					imageBarrier.srcAccessMask = vk::AccessFlagBits2::eNone;
					imageBarrier.srcStageMask  = imageBarrier.dstStageMask;
					state.ImageBarriers.push_back(imageBarrier);
				}
				needsWaitSemaphore = true;
			} else {
				imageBarrier.srcStageMask  = vk::PipelineStageFlagBits2::eNone;
				imageBarrier.srcAccessMask = vk::AccessFlagBits2::eNone;
				state.ImageBarriers.push_back(imageBarrier);

				if (imageBarrier.oldLayout != vk::ImageLayout::eUndefined) {
					throw std::logic_error("Cannot perform immediate image barriers from a layout other than Undefined.");
				}
			}
		}
	}

	if (event.ToFlushAccess || layoutChange) {
		for (auto& e : event.InvalidatedInStage) { e = vk::AccessFlagBits2::eNone; }
	}
	event.ToFlushAccess = vk::AccessFlagBits2::eNone;

	if (needsPipelineBarrier) {
		ForEachBit64(uint64_t(barrier.Stages), [&](uint32_t bit) { event.InvalidatedInStage[bit] |= barrier.Access; });
	} else if (needsWaitSemaphore) {
		state.WaitSemaphores.push_back(waitSemaphore);
		state.WaitStages.push_back(barrier.Stages);

		ForEachBit64(uint64_t(barrier.Stages), [&](uint32_t bit) {
			if (layoutChange) {
				event.InvalidatedInStage[bit] |= barrier.Access;
			} else {
				event.InvalidatedInStage[bit] = static_cast<vk::AccessFlags2>(~0ull);
			}
		});
	}
}

bool RenderGraph::PhysicalPassRequiresWork(const PhysicalPass& physicalPass) const {
	for (auto passIndex : physicalPass.Passes) {
		if (_passes[passIndex]->NeedRenderPass()) { return true; }
	}

	return false;
}

void RenderGraph::PhysicalPassSignal(Vulkan::Device& device,
                                     const PhysicalPass& physicalPass,
                                     PassSubmissionState& state) {
	for (auto& barrier : physicalPass.Flush) {
		if (_physicalDimensions[barrier.ResourceIndex].UsesSemaphore()) { state.NeedSubmissionSemaphore = true; }
	}

	if (state.NeedSubmissionSemaphore) {
		state.ProxySemaphores[0] = device.RequestProxySemaphore();
		state.ProxySemaphores[1] = device.RequestProxySemaphore();
	}
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

void RenderGraph::PassSubmissionState::EmitPrePassBarriers() {}

void RenderGraph::PassSubmissionState::Submit(Vulkan::Device& device) {
	if (!Cmd) { return; }

	for (size_t i = 0; i < WaitSemaphores.size(); ++i) {
		if (WaitSemaphores[i]->GetSemaphore() && !WaitSemaphores[i]->IsPendingWait()) {
			device.AddWaitSemaphore(QueueType, WaitSemaphores[i], WaitStages[i], true);
		}
	}

	if (NeedSubmissionSemaphore) {
		std::vector<Vulkan::SemaphoreHandle> semaphores(2);
		device.Submit(Cmd, nullptr, &semaphores);
		ProxySemaphores[0] = std::move(semaphores[0]);
		ProxySemaphores[1] = std::move(semaphores[1]);
	} else {
		device.Submit(Cmd);
	}
}
}  // namespace Luna
