#include <Luna/Utility/Log.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Sampler.hpp>

namespace Luna {
namespace Vulkan {
void SamplerDeleter::operator()(Sampler* sampler) {
	sampler->_device._samplerPool.Free(sampler);
}

Sampler::Sampler(Device& device, const SamplerCreateInfo& info, bool immutable)
		: Cookie(device), _device(device), _createInfo(info), _immutable(immutable) {
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

	Log::Trace("Vulkan", "Sampler created.");
}

Sampler::~Sampler() noexcept {
	if (_sampler) {
		if (_immutable) {
			_device.GetDevice().destroySampler(_sampler);
		} else if (_internalSync) {
			_device.DestroySamplerNoLock(_sampler);
		} else {
			_device.DestroySampler(_sampler);
		}
	}
}

ImmutableSampler::ImmutableSampler(Hash hash, Device& device, const SamplerCreateInfo& samplerCI)
		: HashedObject<ImmutableSampler>(hash), _device(device) {
	_sampler = _device.CreateSampler(samplerCI);
}
}  // namespace Vulkan
}  // namespace Luna
