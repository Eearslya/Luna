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

	void BeginFrame(const vk::Extent2D& fbSize);
	void Render(CommandBuffer& cmd, bool clear = false);
	ImGuiTextureId Texture(ImageView& view, Sampler* sampler, uint32_t arrayLayer = VK_REMAINING_ARRAY_LAYERS);
	ImGuiTextureId Texture(ImageView& view,
	                       StockSampler sampler = StockSampler::LinearClamp,
	                       uint32_t arrayLayer  = VK_REMAINING_ARRAY_LAYERS);

	void BeginDockspace(bool background = true);
	void EndDockspace();
	void UpdateFontAtlas();

 private:
	struct ImGuiTexture : TemporaryHashMapEnabled<ImGuiTexture>, IntrusiveListEnabled<ImGuiTexture> {
		ImGuiTexture(ImageView& view, Sampler* sampler, uint32_t arrayLayer = VK_REMAINING_ARRAY_LAYERS)
				: View(&view), Sampler(sampler), ArrayLayer(arrayLayer) {}

		ImageView* View     = nullptr;
		Sampler* Sampler    = nullptr;
		uint32_t ArrayLayer = VK_REMAINING_ARRAY_LAYERS;
	};

	void SetRenderState(CommandBuffer& cmd, ImDrawData* drawData, uint32_t frameIndex) const;

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
