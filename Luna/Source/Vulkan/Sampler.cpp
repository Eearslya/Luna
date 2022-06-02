#include <Luna/Core/Log.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Sampler.hpp>

namespace Luna {
namespace Vulkan {
Sampler::Sampler(Hash hash, Device& device, const SamplerCreateInfo& info)
		: Cookie(device), HashedObject<Sampler>(hash), _device(device), _createInfo(info) {
	Log::Trace("Vulkan::Sampler", "Creating new Sampler.");

	const vk::SamplerCreateInfo samplerCI({},
	                                      info.MagFilter,
	                                      info.MinFilter,
	                                      info.MipmapMode,
	                                      info.AddressModeU,
	                                      info.AddressModeV,
	                                      info.AddressModeW,
	                                      info.MipLodBias,
	                                      info.AnisotropyEnable,
	                                      info.MaxAnisotropy,
	                                      info.CompareEnable,
	                                      info.CompareOp,
	                                      info.MinLod,
	                                      info.MaxLod,
	                                      info.BorderColor,
	                                      info.UnnormalizedCoordinates);
	_sampler = _device.GetDevice().createSampler(samplerCI);
}

Sampler::~Sampler() noexcept {
	if (_sampler) { _device.GetDevice().destroySampler(_sampler); }
}
}  // namespace Vulkan
}  // namespace Luna
