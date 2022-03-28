#include <Luna/Assets/Material.hpp>
#include <Luna/Graphics/AssetManager.hpp>
#include <Luna/Vulkan/Device.hpp>

namespace Luna {
void MaterialDeleter::operator()(Material* material) {
	auto graphics = Graphics::Get();
	auto& manager = graphics->GetAssetManager();
	manager.FreeMaterial(material);
}

Material::Material() {
	auto graphics = Graphics::Get();
	auto& device  = graphics->GetDevice();
	DataBuffer    = device.CreateBuffer(
    Vulkan::BufferCreateInfo(Vulkan::BufferDomain::Host, sizeof(Data), vk::BufferUsageFlagBits::eUniformBuffer), &Data);
}

Material::~Material() noexcept {}

void Material::Update() {
	auto graphics = Graphics::Get();
	auto& device  = graphics->GetDevice();

	Data.HasAlbedo = bool(Albedo) && Albedo->Ready;
	Data.HasNormal = bool(Normal) && Normal->Ready;
	Data.HasPBR    = bool(PBR) && PBR->Ready;
	Data.DebugView = DebugView;

	const auto dataHash = Hasher(Data).Get();
	if (dataHash != CurrentDataHash) {
		MaterialData* bufferData = reinterpret_cast<MaterialData*>(DataBuffer->Map());
		memcpy(bufferData, &Data, sizeof(Data));
		DataBuffer->Unmap();
	}
	CurrentDataHash = dataHash;
}
}  // namespace Luna
