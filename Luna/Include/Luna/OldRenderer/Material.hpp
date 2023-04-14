#pragma once

#include <Luna/Renderer/Common.hpp>
#include <Luna/Renderer/RenderContext.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/Sampler.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Luna {
enum class AlphaMode { Opaque, Mask, Blend };

struct MaterialData {
	bool operator==(const MaterialData& other) const {
		return BaseColorFactor == other.BaseColorFactor && EmissiveFactor == other.EmissiveFactor &&
		       RoughnessFactor == other.RoughnessFactor && MetallicFactor == other.MetallicFactor &&
		       AlbedoIndex == other.AlbedoIndex && NormalIndex == other.NormalIndex && PBRIndex == other.PBRIndex &&
		       OcclusionIndex == other.OcclusionIndex && EmissiveIndex == other.EmissiveIndex;
	}

	glm::vec4 BaseColorFactor;
	glm::vec4 EmissiveFactor;
	float RoughnessFactor;
	float MetallicFactor;
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
		auto* data            = cmd.AllocateTypedUniformData<MaterialData>(set, binding, 1);
		data->BaseColorFactor = glm::vec4(BaseColorFactor, 1);
		data->EmissiveFactor  = glm::vec4(EmissiveFactor, 0);
		data->RoughnessFactor = Roughness;
		data->MetallicFactor  = Metallic;

		data->AlbedoIndex   = BindTexture(context, Albedo, true, context.GetDefaultImages().Black2D);
		data->NormalIndex   = BindTexture(context, Normal, false, context.GetDefaultImages().Normal2D);
		data->PBRIndex      = BindTexture(context, PBR, false, context.GetDefaultImages().White2D);
		data->EmissiveIndex = BindTexture(context, Emissive, true, context.GetDefaultImages().Black2D);
	}

	MaterialData Data(const RenderContext& context) const {
		MaterialData data    = {};
		data.BaseColorFactor = glm::vec4(BaseColorFactor, 1);
		data.EmissiveFactor  = glm::vec4(EmissiveFactor, 0);
		data.RoughnessFactor = Roughness;
		data.MetallicFactor  = Metallic;

		data.AlbedoIndex   = BindTexture(context, Albedo, true, context.GetDefaultImages().Black2D);
		data.NormalIndex   = BindTexture(context, Normal, false, context.GetDefaultImages().Normal2D);
		data.PBRIndex      = BindTexture(context, PBR, false, context.GetDefaultImages().White2D);
		data.EmissiveIndex = BindTexture(context, Emissive, true, context.GetDefaultImages().Black2D);

		return data;
	}

	glm::vec3 BaseColorFactor = glm::vec3(1, 1, 1);
	glm::vec3 EmissiveFactor  = glm::vec3(0, 0, 0);
	float Metallic            = 0.0f;
	float Roughness           = 0.5f;
	Texture Albedo;
	Texture Normal;
	Texture PBR;
	Texture Occlusion;
	Texture Emissive;
	AlphaMode AlphaMode = AlphaMode::Opaque;
	bool DualSided      = false;

 private:
	uint32_t BindTexture(const RenderContext& context,
	                     const Texture& texture,
	                     bool srgb,
	                     const Vulkan::ImageHandle& fallback) const {
		const Vulkan::Sampler& sampler =
			texture.Sampler ? *texture.Sampler
											: context.GetDevice().GetStockSampler(Vulkan::StockSampler::DefaultGeometryFilterWrap);
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

template <>
struct std::hash<Luna::MaterialData> {
	size_t operator()(const Luna::MaterialData& data) const {
		Luna::Hasher h;
		h.Data(sizeof(data.BaseColorFactor), glm::value_ptr(data.BaseColorFactor));
		h.Data(sizeof(data.EmissiveFactor), glm::value_ptr(data.EmissiveFactor));
		h(data.RoughnessFactor);
		h(data.MetallicFactor);
		h(data.AlbedoIndex);
		h(data.NormalIndex);
		h(data.PBRIndex);
		h(data.OcclusionIndex);
		h(data.EmissiveIndex);

		return static_cast<size_t>(h.Get());
	}
};
