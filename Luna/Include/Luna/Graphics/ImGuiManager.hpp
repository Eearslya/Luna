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
	void Render(Vulkan::CommandBufferHandle& cmd);

 private:
	void SetRenderState(Vulkan::CommandBufferHandle& cmd, ImDrawData* drawData) const;

	Vulkan::Device& _device;
	std::unique_ptr<ImGuiRenderData> _renderData;
	std::unique_ptr<ImGuiWindowData> _windowData;
	Vulkan::ImageHandle _fontTexture;
	Vulkan::Program* _program     = nullptr;
	Vulkan::Sampler* _fontSampler = nullptr;
	Vulkan::BufferHandle _vertexBuffer;
	Vulkan::BufferHandle _indexBuffer;
};
}  // namespace Luna
