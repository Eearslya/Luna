#pragma once

#include <Luna/Renderer/Common.hpp>
#include <Luna/Renderer/RenderContext.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/Sampler.hpp>

namespace Luna {
struct MaterialData {
	uint32_t AlbedoIndex    = 0;
	uint32_t NormalIndex    = 0;
	uint32_t PBRIndex       = 0;
	uint32_t OcclusionIndex = 0;
	uint32_t EmissiveIndex  = 0;
};

struct Texture {
	Luna::Vulkan::ImageHandle Image;
	Luna::Vulkan::SamplerHandle Sampler;
};

class Material : public IntrusivePtrEnabled<Material> {
 public:
	void BindMaterial(Vulkan::CommandBuffer& cmd, RenderContext& context, uint32_t set, uint32_t binding) const {
		auto* data          = cmd.AllocateTypedUniformData<MaterialData>(set, binding, 1);
		data->AlbedoIndex   = BindTexture(cmd, context, Albedo, true, context.GetDefaultImages().Black2D);
		data->NormalIndex   = BindTexture(cmd, context, Normal, false, context.GetDefaultImages().Normal2D);
		data->PBRIndex      = BindTexture(cmd, context, PBR, false, context.GetDefaultImages().White2D);
		data->EmissiveIndex = BindTexture(cmd, context, Emissive, true, context.GetDefaultImages().Black2D);
	}

	Texture Albedo;
	Texture Normal;
	Texture PBR;
	Texture Occlusion;
	Texture Emissive;
	bool DualSided = false;

 private:
	uint32_t BindTexture(Vulkan::CommandBuffer& cmd,
	                     RenderContext& context,
	                     const Texture& texture,
	                     bool srgb,
	                     const Vulkan::ImageHandle& fallback) const {
		const Vulkan::Sampler& sampler =
			texture.Sampler ? *texture.Sampler
											: cmd.GetDevice().GetStockSampler(Vulkan::StockSampler::DefaultGeometryFilterWrap);
		if (texture.Image) {
			if (srgb) {
				return context.SetSrgbTexture(texture.Image->GetView(), sampler);
			} else {
				return context.SetUnormTexture(texture.Image->GetView(), sampler);
			}
		} else {
			return context.SetTexture(fallback->GetView(), sampler);
		}
	}
};
}  // namespace Luna
