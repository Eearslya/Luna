#pragma once

#include "Common.hpp"
#include "Cookie.hpp"
#include "InternalSync.hpp"

namespace Luna {
namespace Vulkan {
struct SamplerCreateInfo {
	vk::Filter MagFilter                = vk::Filter::eNearest;
	vk::Filter MinFilter                = vk::Filter::eNearest;
	vk::SamplerMipmapMode MipmapMode    = vk::SamplerMipmapMode::eNearest;
	vk::SamplerAddressMode AddressModeU = vk::SamplerAddressMode::eRepeat;
	vk::SamplerAddressMode AddressModeV = vk::SamplerAddressMode::eRepeat;
	vk::SamplerAddressMode AddressModeW = vk::SamplerAddressMode::eRepeat;
	float MipLodBias                    = 0.0f;
	vk::Bool32 AnisotropyEnable         = VK_FALSE;
	float MaxAnisotropy                 = 0.0f;
	vk::Bool32 CompareEnable            = VK_FALSE;
	vk::CompareOp CompareOp             = vk::CompareOp::eNever;
	float MinLod                        = 0.0f;
	float MaxLod                        = 0.0f;
	vk::BorderColor BorderColor         = vk::BorderColor::eFloatTransparentBlack;
	vk::Bool32 UnnormalizedCoordinates  = VK_FALSE;
};

class Sampler : public HashedObject<Sampler>, public Cookie, public InternalSync {
 public:
	Sampler(Hash hash, Device& device, const SamplerCreateInfo& info);
	~Sampler() noexcept;

	const SamplerCreateInfo& GetCreateInfo() const {
		return _createInfo;
	}
	vk::Sampler GetSampler() const {
		return _sampler;
	}

 private:
	Device& _device;
	vk::Sampler _sampler;
	SamplerCreateInfo _createInfo;
};
}  // namespace Vulkan
}  // namespace Luna

template <>
struct std::hash<Luna::Vulkan::SamplerCreateInfo> {
	size_t operator()(const Luna::Vulkan::SamplerCreateInfo& info) const {
		Luna::Hasher h;
		h(info.MagFilter);
		h(info.MinFilter);
		h(info.MipmapMode);
		h(info.AddressModeU);
		h(info.AddressModeV);
		h(info.AddressModeW);
		h(info.MipLodBias);
		h(info.AnisotropyEnable);
		h(info.MaxAnisotropy);
		h(info.CompareEnable);
		h(info.CompareOp);
		h(info.MinLod);
		h(info.MaxLod);
		h(info.BorderColor);
		h(info.UnnormalizedCoordinates);
		return static_cast<size_t>(h.Get());
	}
};
