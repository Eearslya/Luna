#include <Luna/Assets/AssetManager.hpp>
#include <Luna/Assets/Texture.hpp>

namespace Luna {
void TextureDeleter::operator()(Texture* texture) {
	auto manager = AssetManager::Get();
	manager->FreeTexture(texture);
}

Texture::Texture() {}

Texture::~Texture() noexcept {}
}  // namespace Luna
