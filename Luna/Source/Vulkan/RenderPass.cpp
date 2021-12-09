#include <Luna/Core/Log.hpp>
#include <Luna/Utility/BitOps.hpp>
#include <Luna/Utility/StackAllocator.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/RenderPass.hpp>

namespace Luna {
namespace Vulkan {
Hash HashRenderPassInfo(const RenderPassInfo& info, bool compatible) {
	Hasher h;

	vk::Format colorFormats[MaxColorAttachments];
	vk::Format depthFormat;
	uint32_t lazy    = 0;
	uint32_t optimal = 0;

	for (uint32_t i = 0; i < info.ColorAttachmentCount; ++i) {
		const auto& image     = info.ColorAttachments[i]->GetImage();
		const auto& imageInfo = image.GetCreateInfo();
		colorFormats[i]       = imageInfo.Format;
		if (imageInfo.Domain == ImageDomain::Transient) { lazy |= 1u << i; }
		if (image.GetLayoutType() == ImageLayoutType::Optimal) { optimal |= 1u << i; }
		h(static_cast<uint32_t>(image.GetSwapchainLayout()));
	}

	if (info.DepthStencilAttachment) {
		if (info.DepthStencilAttachment->GetImage().GetCreateInfo().Domain == ImageDomain::Transient) {
			lazy |= 1u << info.ColorAttachmentCount;
		}
		if (info.DepthStencilAttachment->GetImage().GetLayoutType() == ImageLayoutType::Optimal) {
			optimal |= 1u << info.ColorAttachmentCount;
		}
	}

	if (info.ArrayLayers > 1) {
		h(info.BaseArrayLayer);
		h(info.ArrayLayers);
	} else {
		h(0u);
		h(info.ArrayLayers);
	}

	h(static_cast<uint32_t>(info.Subpasses.size()));
	for (const auto& subpass : info.Subpasses) {
		h(subpass.ColorAttachmentCount);
		h(subpass.InputAttachmentCount);
		h(subpass.ResolveAttachmentCount);
		h(static_cast<uint32_t>(subpass.DSUsage));
		for (const auto& att : subpass.ColorAttachments) { h(att); }
		for (const auto& att : subpass.InputAttachments) { h(att); }
		for (const auto& att : subpass.ResolveAttachments) { h(att); }
	}

	depthFormat =
		info.DepthStencilAttachment ? info.DepthStencilAttachment->GetCreateInfo().Format : vk::Format::eUndefined;
	for (uint32_t i = 0; i < info.ColorAttachmentCount; ++i) { h(colorFormats[i]); }
	h(info.ColorAttachmentCount);
	h(depthFormat);

	if (!compatible) {
		h(static_cast<uint32_t>(info.DSOps));
		h(info.ClearAttachments);
		h(info.LoadAttachments);
		h(info.StoreAttachments);
		h(optimal);
	}

	h(lazy);

	return h.Get();
}

RenderPass::RenderPass(Hash hash, Device& device, const RenderPassInfo& info)
		: HashedObject<RenderPass>(hash), _device(device), _renderPassInfo(info) {
	Log::Trace("[Vulkan::RenderPass] Creating new Render Pass.");

	std::fill(_colorFormats.begin(), _colorFormats.end(), vk::Format::eUndefined);

	const bool multiView = _renderPassInfo.ArrayLayers > 1;
	auto& subpassInfos   = _renderPassInfo.Subpasses;
	// If we weren't given any subpasses, construct one default subpass that includes all of the color attachments.
	if (subpassInfos.size() == 0) {
		RenderPassInfo::SubpassInfo subpass{.ColorAttachmentCount = _renderPassInfo.ColorAttachmentCount,
		                                    .DSUsage = _renderPassInfo.DSOps & DepthStencilOpBits::DepthStencilReadOnly
		                                                 ? DepthStencilUsage::ReadOnly
		                                                 : DepthStencilUsage::ReadWrite};
		for (uint32_t i = 0; i < _renderPassInfo.ColorAttachmentCount; ++i) { subpass.ColorAttachments[i] = i; }

		subpassInfos.push_back(subpass);
	}
	const uint32_t subpassCount = static_cast<uint32_t>(subpassInfos.size());

	// Set up some bits of data we need to keep track of.
	const uint32_t attachmentCount =
		_renderPassInfo.ColorAttachmentCount + (_renderPassInfo.DepthStencilAttachment ? 1 : 0);
	std::array<vk::AttachmentDescription, MaxColorAttachments + 1> attachments;
	uint32_t implicitTransitions  = 0;
	uint32_t implicitBottomOfPipe = 0;

	// Determine our color attachment properties.
	{
		const auto ColorLoadOp = [&](uint32_t index) -> vk::AttachmentLoadOp {
			if ((_renderPassInfo.ClearAttachments & (1u << index)) != 0) {
				return vk::AttachmentLoadOp::eClear;
			} else if ((_renderPassInfo.LoadAttachments & (1u << index)) != 0) {
				return vk::AttachmentLoadOp::eLoad;
			}

			return vk::AttachmentLoadOp::eDontCare;
		};
		const auto ColorStoreOp = [&](uint32_t index) -> vk::AttachmentStoreOp {
			if ((_renderPassInfo.StoreAttachments & (1u << index)) != 0) { return vk::AttachmentStoreOp::eStore; }

			return vk::AttachmentStoreOp::eDontCare;
		};
		for (uint32_t i = 0; i < _renderPassInfo.ColorAttachmentCount; ++i) {
			const auto& image    = _renderPassInfo.ColorAttachments[i]->GetImage();
			const auto imageInfo = image.GetCreateInfo();
			auto& attachment     = attachments[i];

			_colorFormats[i] = imageInfo.Format;
			attachment       = vk::AttachmentDescription({},
                                             _colorFormats[i],
                                             imageInfo.Samples,
                                             ColorLoadOp(i),
                                             ColorStoreOp(i),
                                             vk::AttachmentLoadOp::eDontCare,
                                             vk::AttachmentStoreOp::eDontCare,
                                             vk::ImageLayout::eUndefined,
                                             vk::ImageLayout::eUndefined);

			if (imageInfo.Domain == ImageDomain::Transient) {
				attachment.initialLayout = vk::ImageLayout::eUndefined;
				attachment.storeOp       = vk::AttachmentStoreOp::eDontCare;
				implicitTransitions |= 1u << i;
			} else if (image.IsSwapchainImage()) {
				if (attachment.loadOp == vk::AttachmentLoadOp::eLoad) {
					attachment.initialLayout = image.GetSwapchainLayout();
					implicitBottomOfPipe |= 1u << i;
				}
				attachment.finalLayout = image.GetSwapchainLayout();

				implicitTransitions |= 1u << i;
			} else {
				attachment.initialLayout = image.GetLayout(vk::ImageLayout::eColorAttachmentOptimal);
			}
		}
	}

	// Determine our depth/stencil properties.
	vk::AttachmentLoadOp depthStencilLoadOp   = vk::AttachmentLoadOp::eDontCare;
	vk::AttachmentStoreOp depthStencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	vk::ImageLayout depthStencilLayout        = vk::ImageLayout::eUndefined;
	{
		if (_renderPassInfo.DSOps & DepthStencilOpBits::ClearDepthStencil) {
			depthStencilLoadOp = vk::AttachmentLoadOp::eClear;
		} else if (_renderPassInfo.DSOps & DepthStencilOpBits::LoadDepthStencil) {
			depthStencilLoadOp = vk::AttachmentLoadOp::eLoad;
		}
		if (_renderPassInfo.DSOps & DepthStencilOpBits::StoreDepthStencil) {
			depthStencilStoreOp = vk::AttachmentStoreOp::eStore;
		}
		if (_renderPassInfo.DepthStencilAttachment) {
			if (_renderPassInfo.DSOps & DepthStencilOpBits::DepthStencilReadOnly) {
				depthStencilLayout =
					_renderPassInfo.DepthStencilAttachment->GetImage().GetLayout(vk::ImageLayout::eDepthStencilReadOnlyOptimal);
			} else {
				depthStencilLayout =
					_renderPassInfo.DepthStencilAttachment->GetImage().GetLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
			}
		}

		if (_renderPassInfo.DepthStencilAttachment) {
			const auto& image     = _renderPassInfo.DepthStencilAttachment->GetImage();
			const auto& imageInfo = image.GetCreateInfo();
			auto& attachment      = attachments[_renderPassInfo.ColorAttachmentCount];

			_depthStencilFormat = imageInfo.Format;
			attachment          = vk::AttachmentDescription({},
                                             _depthStencilFormat,
                                             imageInfo.Samples,
                                             depthStencilLoadOp,
                                             depthStencilStoreOp,
                                             vk::AttachmentLoadOp::eDontCare,
                                             vk::AttachmentStoreOp::eDontCare,
                                             vk::ImageLayout::eUndefined,
                                             vk::ImageLayout::eUndefined);

			if (imageInfo.Domain == ImageDomain::Transient) {
				if (attachment.loadOp == vk::AttachmentLoadOp::eLoad) { attachment.loadOp = vk::AttachmentLoadOp::eDontCare; }
				attachment.storeOp = vk::AttachmentStoreOp::eDontCare;
				implicitTransitions |= 1u << _renderPassInfo.ColorAttachmentCount;
			} else {
				attachment.initialLayout = depthStencilLayout;
			}

			if (FormatHasStencil(_depthStencilFormat)) {
				attachment.stencilLoadOp = attachment.loadOp;
				attachment.storeOp       = attachment.storeOp;
			}
		}
	}

	StackAllocator<vk::AttachmentReference, 1024> referenceAllocator;
	StackAllocator<uint32_t, 1024> preserveAllocator;
	std::vector<vk::SubpassDescription> subpasses(subpassCount);

	// Set up our attachment references.
	{
		for (uint32_t i = 0; i < subpassCount; ++i) {
			auto& subpass  = subpasses[i];
			auto* colors   = referenceAllocator.AllocateCleared(subpassInfos[i].ColorAttachmentCount);
			auto* inputs   = referenceAllocator.AllocateCleared(subpassInfos[i].InputAttachmentCount);
			auto* resolves = referenceAllocator.AllocateCleared(subpassInfos[i].ColorAttachmentCount);
			auto* depth    = referenceAllocator.AllocateCleared(1);

			subpass = vk::SubpassDescription({},
			                                 vk::PipelineBindPoint::eGraphics,
			                                 subpassInfos[i].InputAttachmentCount,
			                                 inputs,
			                                 subpassInfos[i].ColorAttachmentCount,
			                                 colors,
			                                 nullptr,
			                                 depth,
			                                 0,
			                                 nullptr);

			if (subpassInfos[i].ResolveAttachmentCount) { subpass.pResolveAttachments = resolves; }

			for (uint32_t att = 0; att < subpass.colorAttachmentCount; ++att) {
				auto index             = subpassInfos[i].ColorAttachments[att];
				colors[att].attachment = index;
				colors[att].layout     = vk::ImageLayout::eUndefined;
			}
			for (uint32_t att = 0; att < subpass.inputAttachmentCount; ++att) {
				auto index             = subpassInfos[i].InputAttachments[att];
				inputs[att].attachment = index;
				inputs[att].layout     = vk::ImageLayout::eUndefined;
			}
			if (subpass.pResolveAttachments) {
				for (uint32_t att = 0; att < subpass.colorAttachmentCount; ++att) {
					auto index               = subpassInfos[i].ResolveAttachments[att];
					resolves[att].attachment = index;
					resolves[att].layout     = vk::ImageLayout::eUndefined;
				}
			}

			if (_renderPassInfo.DepthStencilAttachment && subpassInfos[i].DSUsage != DepthStencilUsage::None) {
				depth->attachment = _renderPassInfo.ColorAttachmentCount;
			} else {
				depth->attachment = VK_ATTACHMENT_UNUSED;
			}
			depth->layout = vk::ImageLayout::eUndefined;
		}
	}

	// Discern how each attachment is used throughout the subpasses, and decide what layout they need to be each step of
	// the way.
	uint32_t colorAttachmentReadWrite         = 0;
	uint32_t colorSelfDependencies            = 0;
	uint32_t depthStencilAttachmentRead       = 0;
	uint32_t depthStencilAttachmentWrite      = 0;
	uint32_t depthSelfDependencies            = 0;
	uint32_t externalColorDependencies        = 0;
	uint32_t externalDepthDependencies        = 0;
	uint32_t externalInputDependencies        = 0;
	uint32_t externalBottomOfPipeDependencies = 0;
	uint32_t inputAttachmentRead              = 0;
	{
		// Helper functions to locate the attachment references we just assigned.
		const auto FindColor = [&](uint32_t subpass, uint32_t attachment) -> vk::AttachmentReference* {
			auto* colors = subpasses[subpass].pColorAttachments;
			for (uint32_t i = 0; i < subpasses[subpass].colorAttachmentCount; ++i) {
				if (colors[i].attachment == attachment) { return const_cast<vk::AttachmentReference*>(&colors[i]); }
			}
			return nullptr;
		};
		const auto FindInput = [&](uint32_t subpass, uint32_t attachment) -> vk::AttachmentReference* {
			auto* inputs = subpasses[subpass].pInputAttachments;
			for (uint32_t i = 0; i < subpasses[subpass].inputAttachmentCount; ++i) {
				if (inputs[i].attachment == attachment) { return const_cast<vk::AttachmentReference*>(&inputs[i]); }
			}
			return nullptr;
		};
		const auto FindResolve = [&](uint32_t subpass, uint32_t attachment) -> vk::AttachmentReference* {
			if (!subpasses[subpass].pResolveAttachments) { return nullptr; }

			auto* resolves = subpasses[subpass].pResolveAttachments;
			for (uint32_t i = 0; i < subpasses[subpass].colorAttachmentCount; ++i) {
				if (resolves[i].attachment == attachment) { return const_cast<vk::AttachmentReference*>(&resolves[i]); }
			}
			return nullptr;
		};
		const auto FindDepthStencil = [&](uint32_t subpass, uint32_t attachment) -> vk::AttachmentReference* {
			if (subpasses[subpass].pDepthStencilAttachment->attachment == attachment) {
				return const_cast<vk::AttachmentReference*>(subpasses[subpass].pDepthStencilAttachment);
			} else {
				return nullptr;
			}
		};

		std::array<uint32_t, MaxColorAttachments + 1> lastSubpassForAttachment = {};
		std::array<uint32_t, MaxColorAttachments + 1> preserveMasks            = {};

		for (uint32_t attachment = 0; attachment < attachmentCount; ++attachment) {
			const auto attBit  = 1u << attachment;
			bool used          = false;
			auto currentLayout = attachments[attachment].initialLayout;

			for (uint32_t subpass = 0; subpass < subpassCount; ++subpass) {
				const auto subpassBit = 1u << subpass;
				auto* color           = FindColor(subpass, attachment);
				auto* input           = FindInput(subpass, attachment);
				auto* resolve         = FindResolve(subpass, attachment);
				auto* depth           = FindDepthStencil(subpass, attachment);

				// Make sure this attachment is used appropriately.
				if (color || resolve) { assert(!depth); }
				if (depth) { assert(!color && !resolve); }
				if (resolve) { assert(!color && !depth); }

				// If the attachment is not used here, but it has been used, make sure we preserve it.
				if (!color && !input && !resolve && !depth) {
					if (used) { preserveMasks[attachment] |= subpassBit; }
					continue;
				}

				// If the attachment has not been used yet and needs a transition, we mark it for an external dependency.
				if (!used && implicitTransitions & attBit) {
					if (color) { externalColorDependencies |= subpassBit; }
					if (input) { externalInputDependencies |= subpassBit; }
					if (depth) { externalDepthDependencies |= subpassBit; }
				}

				if (input && resolve) {
					// If this attachment is both input and resolve, the only layout we can use is General.
					currentLayout   = vk::ImageLayout::eGeneral;
					input->layout   = currentLayout;
					resolve->layout = currentLayout;

					if (!used && attachments[attachment].initialLayout != vk::ImageLayout::eUndefined) {
						attachments[attachment].initialLayout = currentLayout;
					}

					colorAttachmentReadWrite |= subpassBit;
					inputAttachmentRead |= subpassBit;
				} else if (resolve) {
					if (currentLayout != vk::ImageLayout::eGeneral) { currentLayout = vk::ImageLayout::eColorAttachmentOptimal; }
					resolve->layout = currentLayout;

					colorAttachmentReadWrite |= subpassBit;
				} else if (color && input) {
					// If this attachment is both input and color, the only layout we can use is General.
					currentLayout = vk::ImageLayout::eGeneral;
					color->layout = currentLayout;
					input->layout = currentLayout;

					if (!used && attachments[attachment].initialLayout != vk::ImageLayout::eUndefined) {
						attachments[attachment].initialLayout = currentLayout;
					}

					colorSelfDependencies |= subpassBit;
					colorAttachmentReadWrite |= subpassBit;
					inputAttachmentRead |= subpassBit;
				} else if (color) {
					if (currentLayout != vk::ImageLayout::eGeneral) { currentLayout = vk::ImageLayout::eColorAttachmentOptimal; }
					color->layout = currentLayout;

					colorAttachmentReadWrite |= subpassBit;
				} else if (input && depth) {
					if (subpassInfos[subpass].DSUsage == DepthStencilUsage::ReadWrite) {
						currentLayout = vk::ImageLayout::eGeneral;

						if (!used && attachments[attachment].initialLayout != vk::ImageLayout::eUndefined) {
							attachments[attachment].initialLayout = currentLayout;
						}

						depthSelfDependencies |= subpassBit;
						depthStencilAttachmentWrite |= subpassBit;
					} else {
						if (currentLayout != vk::ImageLayout::eGeneral) {
							currentLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
						}
					}
					input->layout = currentLayout;
					depth->layout = currentLayout;

					inputAttachmentRead |= subpassBit;
					depthStencilAttachmentRead |= subpassBit;
				} else if (depth) {
					if (subpassInfos[subpass].DSUsage == DepthStencilUsage::ReadWrite) {
						if (currentLayout != vk::ImageLayout::eGeneral) {
							currentLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
						}

						depthStencilAttachmentWrite |= subpassBit;
					} else {
						if (currentLayout != vk::ImageLayout::eGeneral) {
							currentLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
						}
					}
					depth->layout = currentLayout;

					depthStencilAttachmentRead |= subpassBit;
				} else if (input) {
					if (currentLayout != vk::ImageLayout::eGeneral) { currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal; }
					input->layout = currentLayout;

					if (!used && attachments[attachment].initialLayout == vk::ImageLayout::eColorAttachmentOptimal) {
						attachments[attachment].initialLayout = currentLayout;
					}

				} else {
					assert(false && "Invalid attachment usage!");
				}

				// If the first subpass changes the attachment layout, we need to add an external dependency for it.
				if (!used && attachments[attachment].initialLayout != currentLayout) {
					if (color || resolve) { externalColorDependencies |= subpassBit; }
					if (input) { externalInputDependencies |= subpassBit; }
					if (depth) { externalInputDependencies |= subpassBit; }
				}

				used                                 = true;
				lastSubpassForAttachment[attachment] = subpass;
			}

			assert(used && "Render pass attachment is never used!");
			if (attachments[attachment].finalLayout == vk::ImageLayout::eUndefined) {
				assert(currentLayout != vk::ImageLayout::eUndefined &&
				       "Render pass attachment is never given a proper layout!");
				attachments[attachment].finalLayout = currentLayout;
			}
		}

		// Don't continue to preserve attachments after the last subpass they've been used in.
		for (uint32_t attachment = 0; attachment < attachmentCount; ++attachment) {
			preserveMasks[attachment] &= (1u << lastSubpassForAttachment[attachment]) - 1;
		}

		// Add preserve attachment refs to each subpass as necessary.
		for (uint32_t subpass = 0; subpass < subpassCount; ++subpass) {
			auto& pass             = subpasses[subpass];
			uint32_t preserveCount = 0;

			for (uint32_t attachment = 0; attachment < attachmentCount; ++attachment) {
				if (preserveMasks[attachment] & (1u << subpass)) { ++preserveCount; }
			}

			auto* preserves              = preserveAllocator.Allocate(preserveCount);
			pass.preserveAttachmentCount = preserveCount;
			pass.pPreserveAttachments    = preserves;
			for (uint32_t attachment = 0; attachment < attachmentCount; ++attachment) {
				if (preserveMasks[attachment] & (1u << subpass)) { *preserves++ = attachment; }
			}
		}
	}

	// Create our external dependencies.
	std::vector<vk::SubpassDependency> subpassDependencies;
	{
		ForEachBit(
			externalColorDependencies | externalInputDependencies | externalDepthDependencies, [&](uint32_t subpass) {
				const auto subpassBit = 1u << subpass;
				subpassDependencies.emplace_back();
				auto& dep      = subpassDependencies.back();
				dep.srcSubpass = VK_SUBPASS_EXTERNAL;
				dep.dstSubpass = subpass;

				if (externalBottomOfPipeDependencies & subpassBit) {
					dep.srcStageMask |= vk::PipelineStageFlagBits::eBottomOfPipe;
				}

				if (externalColorDependencies & subpassBit) {
					dep.srcStageMask |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
					dep.dstStageMask |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
					dep.srcAccessMask |= vk::AccessFlagBits::eColorAttachmentWrite;
					dep.dstAccessMask |= vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
				}

				if (externalInputDependencies & subpassBit) {
					dep.srcStageMask |=
						vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eLateFragmentTests;
					dep.dstStageMask |= vk::PipelineStageFlagBits::eFragmentShader;
					dep.srcAccessMask |=
						vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
					dep.dstAccessMask |= vk::AccessFlagBits::eInputAttachmentRead;
				}

				if (externalDepthDependencies & subpassBit) {
					dep.srcStageMask |= vk::PipelineStageFlagBits::eLateFragmentTests;
					dep.dstStageMask |=
						vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
					dep.srcAccessMask |= vk::AccessFlagBits::eDepthStencilAttachmentWrite;
					dep.dstAccessMask |=
						vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
				}
			});
		ForEachBit(colorSelfDependencies | depthSelfDependencies, [&](uint32_t subpass) {
			const auto subpassBit = 1u << subpass;
			subpassDependencies.emplace_back();
			auto& dep      = subpassDependencies.back();
			dep.srcSubpass = subpass;
			dep.dstSubpass = subpass;
			dep.dependencyFlags |= vk::DependencyFlagBits::eByRegion;
			if (multiView) { dep.dependencyFlags |= vk::DependencyFlagBits::eViewLocal; }

			if (colorSelfDependencies & subpassBit) {
				dep.srcStageMask |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
				dep.srcAccessMask |= vk::AccessFlagBits::eColorAttachmentWrite;
			}

			if (depthSelfDependencies & subpassBit) {
				dep.srcStageMask |=
					vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
				dep.srcAccessMask |= vk::AccessFlagBits::eDepthStencilAttachmentWrite;
			}

			dep.dstStageMask  = vk::PipelineStageFlagBits::eFragmentShader;
			dep.dstAccessMask = vk::AccessFlagBits::eInputAttachmentRead;
		});
	}

	// Set up our dependencies between subpasses.
	{
		for (uint32_t subpass = 1; subpass < subpassCount; ++subpass) {
			const auto subpassBit     = 1u << subpass;
			const auto lastSubpassBit = subpassBit >> 1;
			subpassDependencies.emplace_back();
			auto& dep           = subpassDependencies.back();
			dep.srcSubpass      = subpass - 1;
			dep.dstSubpass      = subpass;
			dep.dependencyFlags = vk::DependencyFlagBits::eByRegion;
			if (multiView) { dep.dependencyFlags |= vk::DependencyFlagBits::eViewLocal; }

			if (colorAttachmentReadWrite & lastSubpassBit) {
				dep.srcStageMask |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
				dep.srcAccessMask |= vk::AccessFlagBits::eColorAttachmentWrite;
			}

			if (depthStencilAttachmentWrite & lastSubpassBit) {
				dep.srcStageMask |=
					vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
				dep.srcAccessMask |= vk::AccessFlagBits::eDepthStencilAttachmentWrite;
			}

			if (colorAttachmentReadWrite & subpassBit) {
				dep.dstStageMask |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
				dep.dstAccessMask |= vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
			}

			if (inputAttachmentRead & subpassBit) {
				dep.dstStageMask |= vk::PipelineStageFlagBits::eFragmentShader;
				dep.dstAccessMask |= vk::AccessFlagBits::eInputAttachmentRead;
			}

			if (depthStencilAttachmentRead & subpassBit) {
				dep.dstStageMask |=
					vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
				dep.dstAccessMask |= vk::AccessFlagBits::eDepthStencilAttachmentRead;
			}

			if (depthStencilAttachmentWrite & subpassBit) {
				dep.dstStageMask |=
					vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
				dep.dstAccessMask |=
					vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
			}
		}
	}

	// Store subpass information.
	{
		for (uint32_t subpass = 0; subpass < subpassCount; ++subpass) {
			auto& sp = subpasses[subpass];
			_subpasses.emplace_back();
			auto& info = _subpasses.back();

			info.ColorAttachmentCount   = sp.colorAttachmentCount;
			info.InputAttachmentCount   = sp.inputAttachmentCount;
			info.DepthStencilAttachment = *sp.pDepthStencilAttachment;
			memcpy(
				info.ColorAttachments.data(), sp.pColorAttachments, sizeof(*sp.pColorAttachments) * sp.colorAttachmentCount);
			memcpy(
				info.InputAttachments.data(), sp.pInputAttachments, sizeof(*sp.pInputAttachments) * sp.inputAttachmentCount);

			vk::SampleCountFlagBits sampleCount{};
			for (uint32_t attachment = 0; attachment < info.ColorAttachmentCount; ++attachment) {
				if (info.ColorAttachments[attachment].attachment == VK_ATTACHMENT_UNUSED) { continue; }

				const auto samples = attachments[info.ColorAttachments[attachment].attachment].samples;
				if (vk::SampleCountFlags(sampleCount) && (samples != sampleCount)) {
					assert(samples != sampleCount && "Mismatch of sample counts within subpass!");
				}
				sampleCount = samples;
			}
			if (info.DepthStencilAttachment.attachment != VK_ATTACHMENT_UNUSED) {
				const auto samples = attachments[info.DepthStencilAttachment.attachment].samples;
				if (vk::SampleCountFlags(sampleCount) && (samples != sampleCount)) {
					assert(samples != sampleCount && "Mismatch of sample counts within subpass!");
				}
				sampleCount = samples;
			}

			assert(vk::SampleCountFlags(sampleCount) && "No sample counts given for subpass!");
			info.Samples = sampleCount;
		}
	}

	// Create the render pass.
	const vk::RenderPassCreateInfo rpCI({},
	                                    attachmentCount,
	                                    attachments.data(),
	                                    subpassCount,
	                                    subpasses.data(),
	                                    static_cast<uint32_t>(subpassDependencies.size()),
	                                    subpassDependencies.data());
	Log::Trace("[Vulkan::RenderPass] - Final create info:");
	Log::Trace("[Vulkan::RenderPass]   - Attachments ({}):", rpCI.attachmentCount);
	for (uint32_t i = 0; i < rpCI.attachmentCount; ++i) {
		const auto& att = rpCI.pAttachments[i];
		Log::Trace("[Vulkan::RenderPass]     - {} MSAA x{}", att.format, vk::to_string(att.samples));
		Log::Trace("[Vulkan::RenderPass]     - Initial {}, Final {}",
		           vk::to_string(att.initialLayout),
		           vk::to_string(att.finalLayout));
		Log::Trace(
			"[Vulkan::RenderPass]     - LoadOp {}, StoreOp {}", vk::to_string(att.loadOp), vk::to_string(att.storeOp));
		Log::Trace("[Vulkan::RenderPass]     - StencilLoadOp {}, StencilStoreOp {}",
		           vk::to_string(att.stencilLoadOp),
		           vk::to_string(att.stencilStoreOp));
	}
	_renderPass = _device.GetDevice().createRenderPass(rpCI);
}

RenderPass::~RenderPass() noexcept {
	if (_renderPass) { _device.GetDevice().destroyRenderPass(_renderPass); }
}

Framebuffer::Framebuffer(Device& device) : Cookie(device), _device(device) {}

Framebuffer::~Framebuffer() noexcept {}
}  // namespace Vulkan
}  // namespace Luna
