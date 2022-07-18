#pragma once

#include <imgui.h>

#include "Vulkan/Common.hpp"

namespace Luna {
class ImGuiRenderer {
 public:
	ImGuiRenderer(Luna::Vulkan::WSI& wsi);
	~ImGuiRenderer() noexcept;

	void BeginFrame();
	void Render(Luna::Vulkan::CommandBufferHandle& cmd, uint32_t frameIndex, bool clear = false);

	void BeginDockspace();
	void EndDockspace();
	void UpdateFontAtlas();

 private:
	void SetRenderState(Luna::Vulkan::CommandBufferHandle& cmd, ImDrawData* drawData, uint32_t frameIndex) const;

	Luna::Vulkan::WSI& _wsi;

	Luna::Vulkan::ImageHandle _fontTexture;
	std::array<bool, ImGuiMouseButton_COUNT> _mouseJustPressed;
	Luna::Vulkan::Program* _program     = nullptr;
	Luna::Vulkan::Sampler* _fontSampler = nullptr;
	std::vector<Luna::Vulkan::BufferHandle> _vertexBuffers;
	std::vector<Luna::Vulkan::BufferHandle> _indexBuffers;
};
}  // namespace Luna
