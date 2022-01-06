#include <Luna/Assets/Texture.hpp>
#include <Luna/Graphics/AssetManager.hpp>
#include <Luna/Graphics/Graphics.hpp>

namespace Luna {
void TextureDeleter::operator()(Texture* texture) {
	auto graphics = Graphics::Get();
	auto& manager = graphics->GetAssetManager();
	manager.FreeTexture(texture);
}

Texture::Texture() {}

Texture::~Texture() noexcept {}
}  // namespace Luna
