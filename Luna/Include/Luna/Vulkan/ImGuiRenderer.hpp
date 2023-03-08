#pragma once

#include <Luna/Utility/TemporaryHashMap.hpp>
#include <Luna/Vulkan/Common.hpp>

struct ImDrawData;

namespace Luna {
namespace Vulkan {
using ImGuiTextureId = uint64_t;

class ImGuiRenderer {
 public:
	ImGuiRenderer(WSI& wsi);
	~ImGuiRenderer() noexcept;

	static ImGuiRenderer* Get() {
		return _instance;
	}

	void BeginFrame();
	void Render(bool clear = false);
	ImGuiTextureId Texture(ImageViewHandle view, Sampler* sampler, uint32_t arrayLayer = VK_REMAINING_ARRAY_LAYERS);
	ImGuiTextureId Texture(ImageViewHandle view,
	                       StockSampler sampler = StockSampler::LinearClamp,
	                       uint32_t arrayLayer  = VK_REMAINING_ARRAY_LAYERS);

	void BeginDockspace(bool background = true);
	void EndDockspace();
	void UpdateFontAtlas();

 private:
	struct ImGuiTexture : TemporaryHashMapEnabled<ImGuiTexture>, IntrusiveListEnabled<ImGuiTexture> {
		ImGuiTexture(ImageViewHandle view, Sampler* sampler, uint32_t arrayLayer = VK_REMAINING_ARRAY_LAYERS)
				: View(view), Sampler(sampler), ArrayLayer(arrayLayer) {}

		ImageViewHandle View;
		Sampler* Sampler    = nullptr;
		uint32_t ArrayLayer = VK_REMAINING_ARRAY_LAYERS;
	};

	void SetRenderState(CommandBufferHandle& cmd, ImDrawData* drawData, uint32_t frameIndex) const;

	static ImGuiRenderer* _instance;

	WSI& _wsi;

	ImageHandle _fontTexture;
	std::array<bool, 16> _mouseJustPressed;
	Program* _program     = nullptr;
	Sampler* _fontSampler = nullptr;
	std::vector<BufferHandle> _vertexBuffers;
	std::vector<BufferHandle> _indexBuffers;
	TemporaryHashMap<ImGuiTexture, 8, false> _textures;
};
}  // namespace Vulkan
}  // namespace Luna
