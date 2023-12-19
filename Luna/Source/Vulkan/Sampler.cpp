#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Sampler.hpp>

namespace Luna {
namespace Vulkan {
void SamplerDeleter::operator()(Sampler* sampler) {
	sampler->_device._samplerPool.Free(sampler);
}

Sampler::Sampler(Device& device, const SamplerCreateInfo& info, bool immutable)
		: Cookie(device), _device(device), _createInfo(info), _immutable(immutable) {
	const vk::SamplerReductionModeCreateInfo reduction(info.ReductionMode);

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

	const vk::StructureChain chain(samplerCI, reduction);
	_sampler = _device.GetDevice().createSampler(chain.get());

	Log::Trace("Vulkan", "{} created.", _immutable ? "Immutable Sampler" : "Sampler");
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
