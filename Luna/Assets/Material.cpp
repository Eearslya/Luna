#include "Material.hpp"

#include "Vulkan/Device.hpp"

namespace Luna {
void Material::Update(Vulkan::Device& device) const {
	Data.BaseColorFactor = BaseColorFactor;
	Data.EmissiveFactor  = glm::vec4(EmissiveFactor, 0.0f);
	Data.HasAlbedo       = bool(Albedo);
	Data.HasNormal       = bool(Normal);
	Data.HasPBR          = bool(PBR);
	Data.HasEmissive     = bool(Emissive);
	Data.AlphaMode       = Alpha == AlphaMode::Mask ? 1 : 0;
	Data.AlphaCutoff     = AlphaCutoff;
	Data.Metallic        = MetallicFactor;
	Data.Roughness       = RoughnessFactor;

	const auto dataHash = Hasher(Data).Get();
	const bool update   = dataHash != DataHash || !DataBuffer;
	if (update) {
		if (!DataBuffer) {
			DataBuffer = device.CreateBuffer(
				Vulkan::BufferCreateInfo(Vulkan::BufferDomain::Host, sizeof(Data), vk::BufferUsageFlagBits::eUniformBuffer),
				&Data);
		}

		MaterialData* bufferData = reinterpret_cast<MaterialData*>(DataBuffer->Map());
		memcpy(bufferData, &Data, sizeof(Data));
	}
	DataHash = dataHash;
}
}  // namespace Luna
