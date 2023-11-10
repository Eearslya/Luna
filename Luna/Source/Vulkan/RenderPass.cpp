#include <Luna/Utility/BitOps.hpp>
#include <Luna/Utility/StackAllocator.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/RenderPass.hpp>

namespace Luna {
namespace Vulkan {
RenderPass::RenderPass(Hash hash, Device& device, const RenderPassInfo& rpInfo)
		: HashedObject<RenderPass>(hash), _device(device) {
	_colorAttachmentFormats.fill(vk::Format::eUndefined);

	// Require explicitly enabling transient load/store, as it has performance impacts.
	const bool enableTransientLoad(rpInfo.Flags & RenderPassFlagBits::EnableTransientLoad);
	const bool enableTransientStore(rpInfo.Flags & RenderPassFlagBits::EnableTransientStore);
	const bool multiview = rpInfo.ArrayLayers > 1;

	auto subpasses = rpInfo.Subpasses;
	// Create a default subpass including all attachments if none are given to us.
	if (subpasses.empty()) {
		RenderPassInfo::Subpass defaultSubpass = {};

		// Add all of the color attachments to the subpass.
		defaultSubpass.ColorAttachmentCount = rpInfo.ColorAttachmentCount;
		for (uint32_t i = 0; i < rpInfo.ColorAttachmentCount; ++i) { defaultSubpass.ColorAttachments[i] = i; }

		// Set up our depth/stencil usage based on the render pass.
		if (rpInfo.Flags & RenderPassFlagBits::DepthStencilReadOnly) {
			defaultSubpass.DepthStencil = DepthStencilUsage::ReadOnly;
		} else {
			defaultSubpass.DepthStencil = DepthStencilUsage::ReadWrite;
		}

		subpasses.push_back(defaultSubpass);
	}

	// First, we need to organize all of our attachments and determine how they'll be used.
	const uint32_t attachmentCount = rpInfo.ColorAttachmentCount + (rpInfo.DepthStencilAttachment ? 1 : 0);
	std::array<vk::AttachmentDescription2, MaxColorAttachments + 1> attachments;
	uint32_t implicitBottomOfPipeMask = 0;
	uint32_t implicitTransitionMask   = 0;

	// Set up all of our color attachments.
	for (uint32_t i = 0; i < rpInfo.ColorAttachmentCount; ++i) {
		const auto& image   = rpInfo.ColorAttachments[i]->GetImage();
		const uint32_t mask = 1u << i;
		auto& att           = attachments[i];

		// Save the format of the attachment for later.
		_colorAttachmentFormats[i] = rpInfo.ColorAttachments[i]->GetFormat();

		// Initialize the attachment struct.
		att = vk::AttachmentDescription2({},
		                                 _colorAttachmentFormats[i],
		                                 image.GetCreateInfo().Samples,
		                                 vk::AttachmentLoadOp::eDontCare,
		                                 vk::AttachmentStoreOp::eDontCare,
		                                 vk::AttachmentLoadOp::eDontCare,
		                                 vk::AttachmentStoreOp::eDontCare,
		                                 vk::ImageLayout::eUndefined,
		                                 vk::ImageLayout::eUndefined);

		// Determine attachment load op.
		if (rpInfo.ClearAttachmentMask & mask) {
			att.loadOp = vk::AttachmentLoadOp::eClear;
		} else if (rpInfo.LoadAttachmentMask & mask) {
			att.loadOp = vk::AttachmentLoadOp::eLoad;
		}

		// Determine attachment store op.
		if (rpInfo.StoreAttachmentMask & mask) { att.storeOp = vk::AttachmentStoreOp::eStore; }

		if (image.GetCreateInfo().Domain == ImageDomain::Transient) {
			// If this is a transient attachment, enforce transient load/store requirements.
			if (enableTransientLoad) { att.initialLayout = image.GetLayout(vk::ImageLayout::eColorAttachmentOptimal); }
			if (!enableTransientStore) { att.storeOp = vk::AttachmentStoreOp::eDontCare; }

			implicitTransitionMask |= mask;
		} else if (image.IsSwapchainImage()) {
			// If the attachment is a swapchain image, we determine its initial layout based on whether we preserve its
			// contents.
			if (att.loadOp == vk::AttachmentLoadOp::eLoad) {
				att.initialLayout = image.GetSwapchainLayout();
				implicitBottomOfPipeMask |= mask;
			} else {
				att.initialLayout = vk::ImageLayout::eUndefined;
			}

			att.finalLayout = image.GetSwapchainLayout();
			implicitTransitionMask |= mask;
		} else {
			// If this is an otherwise normal image, load it as color attachment or general.
			att.initialLayout = image.GetLayout(vk::ImageLayout::eColorAttachmentOptimal);
		}
	}

	// Set up our depth-stencil attachment.
	const bool depthStencilReadOnly(rpInfo.Flags & RenderPassFlagBits::DepthStencilReadOnly);
	vk::ImageLayout depthStencilLayout      = vk::ImageLayout::eUndefined;
	vk::AttachmentLoadOp depthStencilLoad   = vk::AttachmentLoadOp::eDontCare;
	vk::AttachmentStoreOp depthStencilStore = vk::AttachmentStoreOp::eDontCare;
	if (rpInfo.DepthStencilAttachment) {
		_depthStencilFormat = rpInfo.DepthStencilAttachment->GetFormat();

		const auto& image = rpInfo.DepthStencilAttachment->GetImage();
		auto& att         = attachments[rpInfo.ColorAttachmentCount];

		depthStencilLayout =
			image.GetLayout(depthStencilReadOnly ? vk::ImageLayout::eDepthStencilReadOnlyOptimal
		                                       : vk::ImageLayout::eDepthReadOnlyStencilAttachmentOptimal);

		if (rpInfo.Flags & RenderPassFlagBits::ClearDepthStencil) {
			depthStencilLoad = vk::AttachmentLoadOp::eClear;
		} else if (rpInfo.Flags & RenderPassFlagBits::LoadDepthStencil) {
			depthStencilLoad = vk::AttachmentLoadOp::eLoad;
		}

		if (rpInfo.Flags & RenderPassFlagBits::StoreDepthStencil) { depthStencilStore = vk::AttachmentStoreOp::eStore; }

		att = vk::AttachmentDescription2({},
		                                 _depthStencilFormat,
		                                 image.GetCreateInfo().Samples,
		                                 depthStencilLoad,
		                                 depthStencilStore,
		                                 vk::AttachmentLoadOp::eDontCare,
		                                 vk::AttachmentStoreOp::eDontCare,
		                                 vk::ImageLayout::eUndefined,
		                                 vk::ImageLayout::eUndefined);

		if (FormatAspectFlags(_depthStencilFormat) & vk::ImageAspectFlagBits::eStencil) {
			att.stencilLoadOp  = att.loadOp;
			att.stencilStoreOp = att.storeOp;
		}

		if (image.GetCreateInfo().Domain == ImageDomain::Transient) {
			if (enableTransientLoad) {
				att.initialLayout = depthStencilLayout;
			} else {
				if (att.loadOp == vk::AttachmentLoadOp::eLoad) { att.loadOp = vk::AttachmentLoadOp::eDontCare; }
				if (att.stencilLoadOp == vk::AttachmentLoadOp::eLoad) { att.stencilLoadOp = vk::AttachmentLoadOp::eDontCare; }
				att.initialLayout = vk::ImageLayout::eUndefined;
			}

			if (!enableTransientStore) {
				att.storeOp        = vk::AttachmentStoreOp::eDontCare;
				att.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			}

			implicitTransitionMask |= 1u << rpInfo.ColorAttachmentCount;
		} else {
			att.initialLayout = depthStencilLayout;
		}
	}

	StackAllocator<vk::AttachmentReference2, 1024> referenceAllocator;
	StackAllocator<uint32_t, 1024> preserveAllocator;
	std::vector<vk::SubpassDescription2> subpassDescriptions(subpasses.size());
	std::vector<vk::SubpassDependency2> subpassDependencies;

	// First, fill in all of the attachment references. Image layouts are determined later.
	for (size_t i = 0; i < subpasses.size(); ++i) {
		const auto& subpass = subpasses[i];
		auto& desc          = subpassDescriptions[i];

		auto* colors   = referenceAllocator.AllocateCleared(subpass.ColorAttachmentCount);
		auto* inputs   = referenceAllocator.AllocateCleared(subpass.InputAttachmentCount);
		auto* resolves = referenceAllocator.AllocateCleared(subpass.ResolveAttachmentCount);
		auto* depth    = referenceAllocator.AllocateCleared(1);

		// Initialize the subpass description.
		desc = vk::SubpassDescription2({},
		                               vk::PipelineBindPoint::eGraphics,
		                               0,
		                               subpass.InputAttachmentCount,
		                               inputs,
		                               subpass.ColorAttachmentCount,
		                               colors,
		                               nullptr,
		                               depth,
		                               0,
		                               nullptr);

		// Add in resolve attachments if we have them.
		if (subpass.ResolveAttachmentCount) { desc.pResolveAttachments = resolves; }

		// Assign all of our attachment reference indices, leave the layouts for later.
		for (uint32_t j = 0; j < subpass.ColorAttachmentCount; ++j) {
			colors[j] = vk::AttachmentReference2(subpass.ColorAttachments[j], vk::ImageLayout::eUndefined);
		}
		for (uint32_t j = 0; j < subpass.InputAttachmentCount; ++j) {
			inputs[j] = vk::AttachmentReference2(subpass.InputAttachments[j], vk::ImageLayout::eUndefined);
			if (subpass.InputAttachments[j] != VK_ATTACHMENT_UNUSED) {
				if (subpass.InputAttachments[j] < rpInfo.ColorAttachmentCount) {
					inputs[j].aspectMask = vk::ImageAspectFlagBits::eColor;
				} else {
					inputs[j].aspectMask = FormatAspectFlags(_depthStencilFormat);
				}
			}
		}
		for (uint32_t j = 0; j < subpass.ResolveAttachmentCount; ++j) {
			resolves[j] = vk::AttachmentReference2(subpass.ResolveAttachments[j], vk::ImageLayout::eUndefined);
		}
		if (rpInfo.DepthStencilAttachment && subpass.DepthStencil != DepthStencilUsage::None) {
			*depth = vk::AttachmentReference2(rpInfo.ColorAttachmentCount, vk::ImageLayout::eUndefined);
		} else {
			*depth = vk::AttachmentReference2(VK_ATTACHMENT_UNUSED, vk::ImageLayout::eUndefined);
		}
	}

	const auto FindColor = [&](uint32_t subpass, uint32_t att) -> vk::AttachmentReference2* {
		auto* colors = subpassDescriptions[subpass].pColorAttachments;
		for (uint32_t i = 0; i < subpassDescriptions[subpass].colorAttachmentCount; ++i) {
			// &colors[i] refers to the mutable memory backed by referenceAllocator, so const_cast should be safe here.
			if (colors[i].attachment == att) { return const_cast<vk::AttachmentReference2*>(&colors[i]); }
		}
		return nullptr;
	};
	const auto FindInput = [&](uint32_t subpass, uint32_t att) -> vk::AttachmentReference2* {
		auto* inputs = subpassDescriptions[subpass].pInputAttachments;
		for (uint32_t i = 0; i < subpassDescriptions[subpass].inputAttachmentCount; ++i) {
			// &inputs[i] refers to the mutable memory backed by referenceAllocator, so const_cast should be safe here.
			if (inputs[i].attachment == att) { return const_cast<vk::AttachmentReference2*>(&inputs[i]); }
		}
		return nullptr;
	};
	const auto FindResolve = [&](uint32_t subpass, uint32_t att) -> vk::AttachmentReference2* {
		if (!subpassDescriptions[subpass].pResolveAttachments) { return nullptr; }

		auto* resolves = subpassDescriptions[subpass].pResolveAttachments;
		for (uint32_t i = 0; i < subpassDescriptions[subpass].colorAttachmentCount; ++i) {
			// &resolves[i] refers to the mutable memory backed by referenceAllocator, so const_cast should be safe here.
			if (resolves[i].attachment == att) { return const_cast<vk::AttachmentReference2*>(&resolves[i]); }
		}
		return nullptr;
	};
	const auto FindDepthStencil = [&](uint32_t subpass, uint32_t att) -> vk::AttachmentReference2* {
		if (subpassDescriptions[subpass].pDepthStencilAttachment->attachment == att) {
			// pDepthStencilAttachment refers to the mutable memory backed by referenceAllocator, so const_cast should be safe
			// here.
			return const_cast<vk::AttachmentReference2*>(subpassDescriptions[subpass].pDepthStencilAttachment);
		}
		return nullptr;
	};

	std::array<uint32_t, MaxColorAttachments + 1> preserveMasks;
	preserveMasks.fill(0);
	std::array<uint32_t, MaxColorAttachments + 1> lastSubpassForAttachment;
	lastSubpassForAttachment.fill(0);

	// The following are bitmasks that describe the behavior of each subpass, based on the attachments included and how
	// they are used.
	// Each bit represents 1 subpass, from 0 to 31.
	// Each bitmask's meaning is described below.
	// - This subpass reads from and writes to the same color attachment at once.
	uint32_t colorSelfDependencyMask = 0;
	// - This subpass reads from and writes to the depth/stencil attachment at once.
	uint32_t depthSelfDependencyMask = 0;
	// - This subpass reads from one or more input attachments.
	uint32_t inputAttachmentReadMask = 0;
	// - This subpass reads from, or writes to, one or more color or resolve attachments.
	uint32_t colorAttachmentReadWriteMask = 0;
	// - This subpass reads from the depth/stencil attachment.
	uint32_t depthStencilAttachmentReadMask = 0;
	// - This subpass writes to the depth/stencil attachment.
	uint32_t depthStencilAttachmentWriteMask = 0;
	// - This subpass must perform an external dependency to transition a color attachment's layout for the first time.
	uint32_t externalColorDependencyMask = 0;
	// - This subpass must perform an external dependency to transition the depth/stencil attachment's layout for the
	// first time.
	uint32_t externalDepthDependencyMask = 0;
	// - This subpass must perform an external dependency to transition an input attachment's layout for the first time.
	uint32_t externalInputDependencyMask = 0;
	// -
	uint32_t externalBottomOfPipeMask = 0;

	for (uint32_t attachment = 0; attachment < attachmentCount; ++attachment) {
		const uint32_t attMask = 1u << attachment;
		// Keeps track of whether this attachment has been used yet.
		bool used = false;
		// Keeps track of this attachment's layout as we progress through subpasses.
		auto currentLayout  = attachments[attachment].initialLayout;
		auto& initialLayout = attachments[attachment].initialLayout;

		for (uint32_t subpass = 0; subpass < subpasses.size(); ++subpass) {
			const uint32_t subpassMask = 1u << subpass;

			auto* color   = FindColor(subpass, attachment);
			auto* input   = FindInput(subpass, attachment);
			auto* resolve = FindResolve(subpass, attachment);
			auto* depth   = FindDepthStencil(subpass, attachment);

			if (!color && !input && !resolve && !depth) {
				if (used) { preserveMasks[attachment] |= subpassMask; }
				continue;
			}

			if (!used && (implicitTransitionMask & attMask)) {
				if (color) { externalColorDependencyMask |= subpassMask; }
				if (input) { externalInputDependencyMask |= subpassMask; }
				if (depth) { externalDepthDependencyMask |= subpassMask; }
			}

			if (!used && (implicitBottomOfPipeMask & attMask)) { externalBottomOfPipeMask |= subpassMask; }

			if (input && resolve) {
				currentLayout   = vk::ImageLayout::eGeneral;
				input->layout   = currentLayout;
				resolve->layout = currentLayout;

				if (!used && initialLayout != vk::ImageLayout::eUndefined) { initialLayout = currentLayout; }

				if (!used && initialLayout != currentLayout) {
					externalColorDependencyMask |= subpassMask;
					externalInputDependencyMask |= subpassMask;
				}

				colorAttachmentReadWriteMask |= subpassMask;
				inputAttachmentReadMask |= subpassMask;
			} else if (resolve) {
				if (currentLayout != vk::ImageLayout::eGeneral) { currentLayout = vk::ImageLayout::eColorAttachmentOptimal; }
				resolve->layout = currentLayout;

				if (!used && initialLayout != currentLayout) { externalColorDependencyMask |= subpassMask; }

				colorAttachmentReadWriteMask |= subpassMask;
			} else if (color && input) {
				currentLayout = vk::ImageLayout::eGeneral;
				color->layout = currentLayout;
				input->layout = currentLayout;

				if (!used && initialLayout != vk::ImageLayout::eUndefined) { initialLayout = currentLayout; }

				if (!used && initialLayout != currentLayout) {
					externalColorDependencyMask |= subpassMask;
					externalInputDependencyMask |= subpassMask;
				}

				colorAttachmentReadWriteMask |= subpassMask;
				inputAttachmentReadMask |= subpassMask;
				colorSelfDependencyMask |= subpassMask;
			} else if (color) {
				if (currentLayout != vk::ImageLayout::eGeneral) { currentLayout = vk::ImageLayout::eColorAttachmentOptimal; }
				color->layout = currentLayout;

				if (!used && initialLayout != currentLayout) { externalColorDependencyMask |= subpassMask; }

				colorAttachmentReadWriteMask |= subpassMask;
			} else if (input && depth) {
				if (subpasses[subpass].DepthStencil == DepthStencilUsage::ReadWrite) {
					currentLayout = vk::ImageLayout::eGeneral;

					if (!used && initialLayout != vk::ImageLayout::eUndefined) { initialLayout = currentLayout; }

					depthStencilAttachmentWriteMask |= subpassMask;
					depthSelfDependencyMask |= subpassMask;
				} else {
					if (currentLayout != vk::ImageLayout::eGeneral) {
						currentLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
					}
				}
				input->layout = currentLayout;
				depth->layout = currentLayout;

				if (!used && initialLayout != currentLayout) {
					externalInputDependencyMask |= subpassMask;
					externalDepthDependencyMask |= subpassMask;
				}

				inputAttachmentReadMask |= subpassMask;
				depthStencilAttachmentReadMask |= subpassMask;
			} else if (depth) {
				if (subpasses[subpass].DepthStencil == DepthStencilUsage::ReadWrite) {
					if (currentLayout != vk::ImageLayout::eGeneral) {
						currentLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
					}

					depthStencilAttachmentWriteMask |= subpassMask;
				} else {
					if (currentLayout != vk::ImageLayout::eGeneral) {
						currentLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
					}
				}
				depth->layout = currentLayout;

				if (!used && initialLayout != currentLayout) { externalDepthDependencyMask |= subpassMask; }

				depthStencilAttachmentReadMask |= subpassMask;
			} else if (input) {
				if (currentLayout != vk::ImageLayout::eGeneral) { currentLayout = vk::ImageLayout::eStencilReadOnlyOptimal; }
				input->layout = currentLayout;

				if (!used && initialLayout == vk::ImageLayout::eColorAttachmentOptimal) { initialLayout = currentLayout; }

				if (!used && initialLayout != currentLayout) { externalInputDependencyMask |= subpassMask; }

				inputAttachmentReadMask |= subpassMask;
			} else {
			}

			if (color || input || resolve || depth) {
				used                                 = true;
				lastSubpassForAttachment[attachment] = subpass;
			}
		}

		if (attachments[attachment].finalLayout == vk::ImageLayout::eUndefined) {
			attachments[attachment].finalLayout = currentLayout;
		}

		if (!used) {
			// Something's probably wrong, we have an attachment here that wasn't used in any subpasses.
		}

		preserveMasks[attachment] &= (1u << lastSubpassForAttachment[attachment]) - 1;
	}

	for (uint32_t subpass = 0; subpass < subpasses.size(); ++subpass) {
		const uint32_t subpassMask = 1u << subpass;
		auto& desc                 = subpassDescriptions[subpass];

		uint32_t preserveCount = 0;
		for (uint32_t attachment = 0; attachment < attachmentCount; ++attachment) {
			if (preserveMasks[attachment] & subpassMask) { ++preserveCount; }
		}

		auto* preserve               = preserveAllocator.AllocateCleared(preserveCount);
		desc.preserveAttachmentCount = preserveCount;
		desc.pPreserveAttachments    = preserve;
		for (uint32_t attachment = 0; attachment < attachmentCount; ++attachment) {
			if (preserveMasks[attachment] & subpassMask) { *preserve++ = attachment; }
		}
	}

	ForEachBit(
		externalColorDependencyMask | externalInputDependencyMask | externalDepthDependencyMask, [&](uint32_t subpass) {
			const uint32_t subpassMask = 1u << subpass;
			auto& dep                  = subpassDependencies.emplace_back();
			dep                        = vk::SubpassDependency2(VK_SUBPASS_EXTERNAL, subpass);

			if (externalBottomOfPipeMask & subpassMask) { dep.srcStageMask |= vk::PipelineStageFlagBits::eBottomOfPipe; }

			if (externalColorDependencyMask & subpassMask) {
				dep.srcStageMask |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
				dep.dstStageMask |= vk::PipelineStageFlagBits::eColorAttachmentOutput;

				dep.srcAccessMask |= vk::AccessFlagBits::eColorAttachmentWrite;
				dep.dstAccessMask |= vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
			}

			if (externalInputDependencyMask & subpassMask) {
				dep.srcStageMask |=
					vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eLateFragmentTests;
				dep.dstStageMask |= vk::PipelineStageFlagBits::eFragmentShader;

				dep.srcAccessMask |=
					vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
				dep.dstAccessMask |= vk::AccessFlagBits::eInputAttachmentRead;
			}

			if (externalDepthDependencyMask & subpassMask) {
				dep.srcStageMask |= vk::PipelineStageFlagBits::eLateFragmentTests;
				dep.dstStageMask |=
					vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;

				dep.srcAccessMask |= vk::AccessFlagBits::eDepthStencilAttachmentWrite;
				dep.dstAccessMask |=
					vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
			}
		});

	ForEachBit(colorSelfDependencyMask | depthSelfDependencyMask, [&](uint32_t subpass) {
		const uint32_t subpassMask = 1u << subpass;
		auto& dep                  = subpassDependencies.emplace_back();
		dep                        = vk::SubpassDependency2(subpass,
                                 subpass,
                                 {},
                                 vk::PipelineStageFlagBits::eFragmentShader,
                                 {},
                                 vk::AccessFlagBits::eInputAttachmentRead,
                                 vk::DependencyFlagBits::eByRegion);

		if (multiview) { dep.dependencyFlags |= vk::DependencyFlagBits::eViewLocal; }

		if (colorSelfDependencyMask & subpassMask) {
			dep.srcStageMask |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
			dep.srcAccessMask |= vk::AccessFlagBits::eColorAttachmentWrite;
		}
		if (depthSelfDependencyMask & subpassMask) {
			dep.srcStageMask |=
				vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
			dep.srcAccessMask |= vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		}
	});

	for (uint32_t subpass = 1; subpass < subpasses.size(); ++subpass) {
		const uint32_t subpassMask     = 1u << subpass;
		const uint32_t lastSubpassMask = 1u << (subpass - 1);
		auto& dep                      = subpassDependencies.emplace_back();
		dep = vk::SubpassDependency2(subpass - 1, subpass, {}, {}, {}, {}, vk::DependencyFlagBits::eByRegion);

		if (multiview) { dep.dependencyFlags |= vk::DependencyFlagBits::eViewLocal; }

		if (colorAttachmentReadWriteMask & lastSubpassMask) {
			dep.srcStageMask |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
			dep.srcAccessMask |= vk::AccessFlagBits::eColorAttachmentWrite;
		}
		if (depthStencilAttachmentWriteMask & lastSubpassMask) {
			dep.srcStageMask |=
				vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
			dep.srcAccessMask |= vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		}

		if (colorAttachmentReadWriteMask & subpassMask) {
			dep.dstStageMask |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
			dep.dstAccessMask |= vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
		}
		if (inputAttachmentReadMask & subpassMask) {
			dep.dstStageMask |= vk::PipelineStageFlagBits::eFragmentShader;
			dep.dstAccessMask |= vk::AccessFlagBits::eInputAttachmentRead;
		}
		if (depthStencilAttachmentReadMask & subpassMask) {
			dep.dstStageMask |=
				vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
			dep.dstAccessMask |= vk::AccessFlagBits::eDepthStencilAttachmentRead;
		}
		if (depthStencilAttachmentWriteMask & subpassMask) {
			dep.dstStageMask |=
				vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
			dep.dstAccessMask |=
				vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		}
	}

	vk::RenderPassCreateInfo2 rpCI({},
	                               attachmentCount,
	                               attachments.data(),
	                               subpassDescriptions.size(),
	                               subpassDescriptions.data(),
	                               subpassDependencies.size(),
	                               subpassDependencies.data());

	SetupSubpasses(rpCI);

	_renderPass = _device.GetDevice().createRenderPass2(rpCI);
	Log::Trace("Vulkan", "Render Pass created.");
}

RenderPass::RenderPass(Hash hash, Device& device, const vk::RenderPassCreateInfo2& rpCI)
		: HashedObject<RenderPass>(hash), _device(device) {}

RenderPass::~RenderPass() noexcept {
	if (_renderPass) { _device.GetDevice().destroyRenderPass(_renderPass); }
}

void RenderPass::SetupSubpasses(const vk::RenderPassCreateInfo2& rpCI) {
	const auto* attachments = rpCI.pAttachments;
	for (uint32_t i = 0; i < rpCI.subpassCount; ++i) {
		const auto& subpass = rpCI.pSubpasses[i];

		SubpassInfo subpassInfo = {.ColorAttachmentCount = subpass.colorAttachmentCount,
		                           .InputAttachmentCount = subpass.inputAttachmentCount};
		if (subpass.pDepthStencilAttachment) {
			subpassInfo.DepthStencilAttachment = *subpass.pDepthStencilAttachment;
		} else {
			subpassInfo.DepthStencilAttachment = vk::AttachmentReference2(VK_ATTACHMENT_UNUSED);
		}
		for (uint32_t j = 0; j < subpass.colorAttachmentCount; ++j) {
			subpassInfo.ColorAttachments[j] = subpass.pColorAttachments[j];
		}
		for (uint32_t j = 0; j < subpass.inputAttachmentCount; ++j) {
			subpassInfo.InputAttachments[j] = subpass.pInputAttachments[j];
		}

		// Sanity checks.
		vk::SampleCountFlagBits samples = {};
		for (uint32_t attachment = 0; attachment < subpassInfo.ColorAttachmentCount; ++attachment) {
			if (subpassInfo.ColorAttachments[attachment].attachment == VK_ATTACHMENT_UNUSED) { continue; }

			const auto attachmentSamples = attachments[subpassInfo.ColorAttachments[attachment].attachment].samples;
			if (bool(samples) && samples != attachmentSamples) {
				Log::Error("Vulkan::RenderPass",
				           "Render Pass Failure: Color attachment {} used in Subpass {} has a sample count of {}, while other "
				           "attachments in this subpass have a sample count of {}. All attachments within a subpass must have "
				           "matching sample counts.",
				           attachment,
				           i,
				           vk::to_string(attachmentSamples),
				           vk::to_string(samples));
				throw std::logic_error("All attachments within a subpass must have the same sample count!");
			}

			samples = attachmentSamples;
		}
		if (subpassInfo.DepthStencilAttachment.attachment != VK_ATTACHMENT_UNUSED) {
			const auto attachmentSamples = attachments[subpassInfo.DepthStencilAttachment.attachment].samples;
			if (bool(samples) && samples != attachmentSamples) {
				Log::Error("Vulkan::RenderPass",
				           "Render Pass Failure: Color attachment {} used in Subpass {} has a sample count of {}, while other "
				           "attachments in this subpass have a sample count of {}. All attachments within a subpass must have "
				           "matching sample counts.",
				           subpassInfo.DepthStencilAttachment.attachment,
				           i,
				           vk::to_string(attachmentSamples),
				           vk::to_string(samples));
				throw std::logic_error("All attachments within a subpass must have the same sample count!");
			}
		}
		subpassInfo.SampleCount = samples;

		_subpasses.push_back(subpassInfo);
	}
}

Framebuffer::Framebuffer(Device& device, const RenderPass& renderPass, const RenderPassInfo& rpInfo)
		: Cookie(device), _device(device), _renderPass(renderPass), _renderPassInfo(rpInfo) {
	_extent = vk::Extent2D(std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max());
	for (uint32_t i = 0; i < rpInfo.ColorAttachmentCount; ++i) {
		_extent.width  = std::min(_extent.width, rpInfo.ColorAttachments[i]->GetWidth());
		_extent.height = std::min(_extent.height, rpInfo.ColorAttachments[i]->GetHeight());
	}
	if (rpInfo.DepthStencilAttachment) {
		_extent.width  = std::min(_extent.width, rpInfo.DepthStencilAttachment->GetWidth());
		_extent.height = std::min(_extent.height, rpInfo.DepthStencilAttachment->GetHeight());
	}

	uint32_t viewCount = 0;
	std::array<vk::ImageView, MaxColorAttachments + 1> views;
	for (uint32_t i = 0; i < rpInfo.ColorAttachmentCount; ++i) {
		if (rpInfo.ArrayLayers > 1) {
			views[viewCount++] = rpInfo.ColorAttachments[i]->GetView();
		} else {
			views[viewCount++] = rpInfo.ColorAttachments[i]->GetRenderTargetView(rpInfo.BaseLayer);
		}
	}
	if (rpInfo.DepthStencilAttachment) {
		if (rpInfo.ArrayLayers > 1) {
			views[viewCount++] = rpInfo.DepthStencilAttachment->GetView();
		} else {
			views[viewCount++] = rpInfo.DepthStencilAttachment->GetRenderTargetView(rpInfo.BaseLayer);
		}
	}

	const vk::FramebufferCreateInfo fbCI(
		{}, _renderPass.GetRenderPass(), viewCount, views.data(), _extent.width, _extent.height, 1);
	_framebuffer = _device.GetDevice().createFramebuffer(fbCI);
	Log::Trace("Vulkan", "Framebuffer created.");
}

Framebuffer::~Framebuffer() noexcept {
	if (_framebuffer) {
		if (_internalSync) {
			_device.DestroyFramebufferNoLock(_framebuffer);
		} else {
			_device.DestroyFramebuffer(_framebuffer);
		}
	}
}

FramebufferNode::FramebufferNode(Device& device, const RenderPass& renderPass, const RenderPassInfo& rpInfo)
		: Framebuffer(device, renderPass, rpInfo) {
	_internalSync = true;
}
}  // namespace Vulkan
}  // namespace Luna
