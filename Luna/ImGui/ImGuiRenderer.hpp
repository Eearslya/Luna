#pragma once

#include <imgui.h>

#include "Utility/TemporaryHashMap.hpp"
#include "Vulkan/Common.hpp"

namespace Luna {
class ImGuiRenderer {
 public:
	ImGuiRenderer(Luna::Vulkan::WSI& wsi);
	~ImGuiRenderer() noexcept;

	void BeginFrame();
	void Render(Luna::Vulkan::CommandBufferHandle& cmd, uint32_t frameIndex, bool clear = false);
	ImTextureID Texture(Vulkan::ImageViewHandle& view,
	                    Vulkan::Sampler* sampler,
	                    uint32_t arrayLayer = VK_REMAINING_ARRAY_LAYERS);
	ImTextureID Texture(Vulkan::ImageViewHandle& view,
	                    Vulkan::StockSampler sampler,
	                    uint32_t arrayLayer = VK_REMAINING_ARRAY_LAYERS);

	void BeginDockspace();
	void EndDockspace();
	void UpdateFontAtlas();

 private:
	struct ImGuiTexture : TemporaryHashMapEnabled<ImGuiTexture>, IntrusiveListEnabled<ImGuiTexture> {
		ImGuiTexture(Vulkan::ImageViewHandle& view,
		             Vulkan::Sampler* sampler,
		             uint32_t arrayLayer = VK_REMAINING_ARRAY_LAYERS)
				: View(view), Sampler(sampler), ArrayLayer(arrayLayer) {}

		Vulkan::ImageViewHandle View;
		Vulkan::Sampler* Sampler = nullptr;
		uint32_t ArrayLayer      = VK_REMAINING_ARRAY_LAYERS;
	};

	void SetRenderState(Luna::Vulkan::CommandBufferHandle& cmd, ImDrawData* drawData, uint32_t frameIndex) const;

	Luna::Vulkan::WSI& _wsi;

	Luna::Vulkan::ImageHandle _fontTexture;
	std::array<bool, ImGuiMouseButton_COUNT> _mouseJustPressed;
	Luna::Vulkan::Program* _program     = nullptr;
	Luna::Vulkan::Sampler* _fontSampler = nullptr;
	std::vector<Luna::Vulkan::BufferHandle> _vertexBuffers;
	std::vector<Luna::Vulkan::BufferHandle> _indexBuffers;
	TemporaryHashMap<ImGuiTexture, 8, false> _textures;
};
}  // namespace Luna
