#pragma once

#include <Luna/Assets/Texture.hpp>
#include <Luna/Graphics/Graphics.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/Common.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/Sampler.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Luna {
struct MaterialData {
	glm::vec4 BaseColorFactor = glm::vec4(1, 1, 1, 1);
	glm::vec4 EmissiveFactor  = glm::vec4(0, 0, 0, 0);
	int HasAlbedo             = 0;
	int HasNormal             = 0;
	int HasPBR                = 0;
	int HasEmissive           = 0;
	float AlphaMask           = 0.0f;
	float AlphaCutoff         = 0.0f;
	float Metallic            = 0.0f;
	float Roughness           = 0.0f;
	float DebugView           = 0.0f;
};

struct Material;

struct MaterialDeleter {
	void operator()(Material* material);
};

struct Material : public IntrusivePtrEnabled<Material, MaterialDeleter, MultiThreadCounter> {
	Material();
	~Material() noexcept;

	void Update();

	bool DualSided = false;
	MaterialData Data;
	Vulkan::BufferHandle DataBuffer;
	TextureHandle Albedo;
	TextureHandle Normal;
	TextureHandle PBR;
	float DebugView = 0.0f;

	Hash CurrentDataHash = {};
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
		h(data.AlphaMask);
		h(data.AlphaCutoff);
		h(data.Metallic);
		h(data.Roughness);
		h(data.DebugView);
		return static_cast<size_t>(h.Get());
	}
};
