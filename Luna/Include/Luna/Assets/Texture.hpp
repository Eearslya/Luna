#pragma once

#include <Luna/Utility/IntrusivePtr.hpp>
#include <Luna/Vulkan/Common.hpp>

namespace Luna {
struct Texture;

struct TextureDeleter {
	void operator()(Texture* texture);
};

struct Texture : public IntrusivePtrEnabled<Texture, TextureDeleter, MultiThreadCounter> {
	Texture();
	~Texture() noexcept;

	Vulkan::ImageHandle Image;
	Vulkan::Sampler* Sampler = nullptr;
	std::atomic_bool Ready   = false;
};

using TextureHandle = IntrusivePtr<Texture>;
}  // namespace Luna
