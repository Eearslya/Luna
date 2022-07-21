#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Texture.hpp"
#include "Utility/Hash.hpp"
#include "Vulkan/Buffer.hpp"

namespace Luna {
enum class AlphaMode { Opaque, Mask, Blend };

struct MaterialData {
	glm::vec4 BaseColorFactor = glm::vec4(1, 1, 1, 1);
	glm::vec4 EmissiveFactor  = glm::vec4(0, 0, 0, 0);
	int HasAlbedo             = 0;
	int HasNormal             = 0;
	int HasPBR                = 0;
	int HasEmissive           = 0;
	int AlphaMode             = 0;
	float AlphaCutoff         = 0.0f;
	float Metallic            = 0.0f;
	float Roughness           = 0.0f;
};

struct Material : public IntrusivePtrEnabled<Material> {
	void Update(Vulkan::Device& device) const;

	bool DualSided = false;

	glm::vec4 BaseColorFactor = glm::vec4(1, 1, 1, 1);
	glm::vec3 EmissiveFactor  = glm::vec3(0, 0, 0);
	TextureHandle Albedo;
	TextureHandle Normal;
	TextureHandle PBR;
	TextureHandle Emissive;
	AlphaMode Alpha       = AlphaMode::Opaque;
	float AlphaCutoff     = 0.5f;
	float MetallicFactor  = 0.0f;
	float RoughnessFactor = 1.0f;

	mutable MaterialData Data;
	mutable Vulkan::BufferHandle DataBuffer;
	mutable Hash DataHash = {};
};
using MaterialHandle = IntrusivePtr<Material>;
}  // namespace Luna

template <>
struct std::hash<Luna::MaterialData> {
	size_t operator()(const Luna::MaterialData& data) {
		Luna::Hasher h;
		h.Data(sizeof(glm::vec4), glm::value_ptr(data.BaseColorFactor));
		h.Data(sizeof(glm::vec4), glm::value_ptr(data.EmissiveFactor));
		h(data.HasAlbedo);
		h(data.HasNormal);
		h(data.HasPBR);
		h(data.HasEmissive);
		h(data.AlphaMode);
		h(data.AlphaCutoff);
		h(data.Metallic);
		h(data.Roughness);
		return static_cast<size_t>(h.Get());
	}
};
