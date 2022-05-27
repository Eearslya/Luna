#pragma once

#include <imgui.h>

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
struct ImGuiRenderData;
struct ImGuiWindowData;

class ImGuiManager {
 public:
	ImGuiManager(Vulkan::Device& device);
	~ImGuiManager() noexcept;

	void BeginFrame();
	void EndFrame();
	void Render(Vulkan::CommandBufferHandle& cmd, uint32_t frameIndex);
	void SetDockspace(bool dockspace);

 private:
	void SetRenderState(Vulkan::CommandBufferHandle& cmd, ImDrawData* drawData, uint32_t frameIndex) const;

	Vulkan::Device& _device;
	std::unique_ptr<ImGuiRenderData> _renderData;
	std::unique_ptr<ImGuiWindowData> _windowData;
	Vulkan::ImageHandle _fontTexture;
	Vulkan::Program* _program     = nullptr;
	Vulkan::Sampler* _fontSampler = nullptr;
	std::vector<Vulkan::BufferHandle> _vertexBuffers;
	std::vector<Vulkan::BufferHandle> _indexBuffers;
	bool _dockspace = false;
};
}  // namespace Luna
