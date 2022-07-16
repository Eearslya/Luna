#include "ImGuiRenderer.hpp"

#include <GLFW/glfw3.h>

#include "Input.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/CommandBuffer.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/RenderPass.hpp"
#include "Vulkan/Sampler.hpp"
#include "Vulkan/WSI.hpp"

using namespace Luna;

struct PushConstant {
	float ScaleX;
	float ScaleY;
	float TranslateX;
	float TranslateY;
};

ImGuiRenderer::ImGuiRenderer(Vulkan::WSI& wsi) : _wsi(wsi) {
	auto& device = wsi.GetDevice();

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();

	// Basic config flags.
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	// Fonts
	{
		io.Fonts->AddFontFromFileTTF("Assets/Fonts/Roboto-SemiMedium.ttf", 16.0f);

		ImFontConfig jpConfig;
		jpConfig.MergeMode = true;
		io.Fonts->AddFontFromFileTTF(
			"Assets/Fonts/NotoSansJP-Medium.otf", 18.0f, &jpConfig, io.Fonts->GetGlyphRangesJapanese());

		ImFontConfig faConfig;
		faConfig.MergeMode                 = true;
		faConfig.PixelSnapH                = true;
		static const ImWchar fontAwesome[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
		io.Fonts->AddFontFromFileTTF("Assets/Fonts/FontAwesome6Free-Regular-400.otf", 16.0f, &faConfig, fontAwesome);
		io.Fonts->AddFontFromFileTTF("Assets/Fonts/FontAwesome6Free-Solid-900.otf", 16.0f, &faConfig, fontAwesome);

		io.Fonts->Build();
	}

	// Custom theming
	{
		ImGuiStyle& style = ImGui::GetStyle();
		ImVec4* colors    = style.Colors;

		colors[ImGuiCol_Text]                  = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
		colors[ImGuiCol_TextDisabled]          = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
		colors[ImGuiCol_WindowBg]              = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
		colors[ImGuiCol_ChildBg]               = ImVec4(0.06f, 0.06f, 0.06f, 0.00f);
		colors[ImGuiCol_PopupBg]               = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
		colors[ImGuiCol_Border]                = ImVec4(0.11f, 0.09f, 0.15f, 1.00f);
		colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_FrameBg]               = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
		colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.03f, 0.03f, 0.03f, 1.00f);
		colors[ImGuiCol_FrameBgActive]         = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
		colors[ImGuiCol_TitleBg]               = ImVec4(0.07f, 0.03f, 0.14f, 1.00f);
		colors[ImGuiCol_TitleBgActive]         = ImVec4(0.08f, 0.00f, 0.20f, 1.00f);
		colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.07f, 0.03f, 0.14f, 1.00f);
		colors[ImGuiCol_MenuBarBg]             = ImVec4(0.03f, 0.03f, 0.03f, 1.00f);
		colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
		colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.09f, 0.06f, 0.14f, 1.00f);
		colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.07f, 0.03f, 0.14f, 1.00f);
		colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.08f, 0.00f, 0.20f, 1.00f);
		colors[ImGuiCol_CheckMark]             = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
		colors[ImGuiCol_SliderGrab]            = ImVec4(0.09f, 0.07f, 0.12f, 1.00f);
		colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.10f, 0.05f, 0.18f, 1.00f);
		colors[ImGuiCol_Button]                = ImVec4(1.00f, 1.00f, 1.00f, 0.00f);
		colors[ImGuiCol_ButtonHovered]         = ImVec4(1.00f, 1.00f, 1.00f, 0.16f);
		colors[ImGuiCol_ButtonActive]          = ImVec4(1.00f, 1.00f, 1.00f, 0.39f);
		colors[ImGuiCol_Header]                = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
		colors[ImGuiCol_HeaderHovered]         = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
		colors[ImGuiCol_HeaderActive]          = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
		colors[ImGuiCol_Separator]             = ImVec4(0.09f, 0.06f, 0.14f, 1.00f);
		colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.07f, 0.03f, 0.14f, 1.00f);
		colors[ImGuiCol_SeparatorActive]       = ImVec4(0.08f, 0.00f, 0.20f, 1.00f);
		colors[ImGuiCol_ResizeGrip]            = ImVec4(0.09f, 0.06f, 0.14f, 1.00f);
		colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.07f, 0.03f, 0.14f, 1.00f);
		colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.08f, 0.00f, 0.20f, 1.00f);
		colors[ImGuiCol_Tab]                   = ImVec4(0.01f, 0.01f, 0.01f, 1.00f);
		colors[ImGuiCol_TabHovered]            = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
		colors[ImGuiCol_TabActive]             = ImVec4(0.03f, 0.03f, 0.03f, 1.00f);
		colors[ImGuiCol_TabUnfocused]          = ImVec4(0.01f, 0.01f, 0.01f, 1.00f);
		colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.03f, 0.03f, 0.03f, 1.00f);
		colors[ImGuiCol_DockingPreview]        = ImVec4(0.18f, 0.00f, 0.49f, 1.00f);
		colors[ImGuiCol_DockingEmptyBg]        = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
		colors[ImGuiCol_PlotLines]             = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
		colors[ImGuiCol_PlotLinesHovered]      = ImVec4(0.18f, 0.00f, 0.49f, 1.00f);
		colors[ImGuiCol_PlotHistogram]         = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
		colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(0.18f, 0.00f, 0.49f, 1.00f);
		colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
		colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
		colors[ImGuiCol_TableBorderLight]      = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
		colors[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
		colors[ImGuiCol_TextSelectedBg]        = ImVec4(1.00f, 1.00f, 1.00f, 0.16f);
		colors[ImGuiCol_DragDropTarget]        = ImVec4(0.18f, 0.00f, 0.49f, 1.00f);
		colors[ImGuiCol_NavHighlight]          = ImVec4(0.18f, 0.00f, 0.49f, 1.00f);
		colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.38f, 0.15f, 0.77f, 1.00f);
		colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.00f, 0.00f, 0.00f, 0.59f);
		colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.00f, 0.00f, 0.00f, 0.59f);

		style.ChildRounding     = 4.0f;
		style.FrameBorderSize   = 1.0f;
		style.FrameRounding     = 2.0f;
		style.GrabMinSize       = 7.0f;
		style.PopupRounding     = 2.0f;
		style.ScrollbarRounding = 12.0f;
		style.ScrollbarSize     = 13.0f;
		style.TabBorderSize     = 1.0f;
		style.TabRounding       = 0.0f;
		style.WindowRounding    = 4.0f;
	}

	// Window data
	{
		io.KeyMap[ImGuiKey_Tab]         = GLFW_KEY_TAB;
		io.KeyMap[ImGuiKey_LeftArrow]   = GLFW_KEY_LEFT;
		io.KeyMap[ImGuiKey_RightArrow]  = GLFW_KEY_RIGHT;
		io.KeyMap[ImGuiKey_UpArrow]     = GLFW_KEY_UP;
		io.KeyMap[ImGuiKey_DownArrow]   = GLFW_KEY_DOWN;
		io.KeyMap[ImGuiKey_PageUp]      = GLFW_KEY_PAGE_UP;
		io.KeyMap[ImGuiKey_PageDown]    = GLFW_KEY_PAGE_DOWN;
		io.KeyMap[ImGuiKey_Home]        = GLFW_KEY_HOME;
		io.KeyMap[ImGuiKey_End]         = GLFW_KEY_END;
		io.KeyMap[ImGuiKey_Insert]      = GLFW_KEY_INSERT;
		io.KeyMap[ImGuiKey_Delete]      = GLFW_KEY_DELETE;
		io.KeyMap[ImGuiKey_Backspace]   = GLFW_KEY_BACKSPACE;
		io.KeyMap[ImGuiKey_Space]       = GLFW_KEY_SPACE;
		io.KeyMap[ImGuiKey_Enter]       = GLFW_KEY_ENTER;
		io.KeyMap[ImGuiKey_Escape]      = GLFW_KEY_ESCAPE;
		io.KeyMap[ImGuiKey_KeyPadEnter] = GLFW_KEY_KP_ENTER;
		io.KeyMap[ImGuiKey_A]           = GLFW_KEY_A;
		io.KeyMap[ImGuiKey_C]           = GLFW_KEY_C;
		io.KeyMap[ImGuiKey_V]           = GLFW_KEY_V;
		io.KeyMap[ImGuiKey_X]           = GLFW_KEY_X;
		io.KeyMap[ImGuiKey_Y]           = GLFW_KEY_Y;
		io.KeyMap[ImGuiKey_Z]           = GLFW_KEY_Z;
	}

	// Renderer data
	{
		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

		constexpr static const char* vertGlsl = R"GLSL(
#version 450 core
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV0;
layout(location = 2) in vec4 inColor;
layout(push_constant) uniform PushConstant { vec2 Scale; vec2 Translate; } PC;
layout(location = 0) out struct { vec4 Color; vec2 UV; } Out;
void main() {
    Out.Color = inColor;
    Out.UV = inUV0;
    gl_Position = vec4(inPosition * PC.Scale + PC.Translate, 0, 1);
}
)GLSL";
		constexpr static const char* fragGlsl = R"GLSL(
#version 450 core
layout(location = 0) in struct { vec4 Color; vec2 UV; } In;
layout(set=0, binding=0) uniform sampler2D Texture;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = In.Color * texture(Texture, In.UV.st);
}
)GLSL";

		_program = device.RequestProgram(vertGlsl, fragGlsl);

		unsigned char* pixels = nullptr;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		const Vulkan::ImageInitialData data{.Data = pixels};
		const auto imageCI = Vulkan::ImageCreateInfo::Immutable2D(
			static_cast<uint32_t>(width), static_cast<uint32_t>(height), vk::Format::eR8G8B8A8Unorm, false);
		_fontTexture = device.CreateImage(imageCI, &data);

		const Vulkan::SamplerCreateInfo samplerCI{.MagFilter        = vk::Filter::eLinear,
		                                          .MinFilter        = vk::Filter::eLinear,
		                                          .MipmapMode       = vk::SamplerMipmapMode::eLinear,
		                                          .AddressModeU     = vk::SamplerAddressMode::eRepeat,
		                                          .AddressModeV     = vk::SamplerAddressMode::eRepeat,
		                                          .AddressModeW     = vk::SamplerAddressMode::eRepeat,
		                                          .AnisotropyEnable = false,
		                                          .MaxAnisotropy    = 1.0f,
		                                          .MinLod           = -1000.0f,
		                                          .MaxLod           = 1000.0f};
		_fontSampler = device.RequestSampler(samplerCI);

		io.Fonts->SetTexID((reinterpret_cast<ImTextureID>(&_fontTexture->GetView())));
	}

	Input::OnChar += [this](char c) {
		ImGuiIO& io = ImGui::GetIO();

		io.AddInputCharacter(c);
	};
	Input::OnKey += [this](Key key, InputAction action, InputMods mods) {
		ImGuiIO& io = ImGui::GetIO();

		constexpr static const int maxKey = sizeof(io.KeysDown) / sizeof(io.KeysDown[0]);
		if (static_cast<int>(key) < maxKey) {
			if (action == InputAction::Press) { io.KeysDown[static_cast<int>(key)] = true; }
			if (action == InputAction::Release) { io.KeysDown[static_cast<int>(key)] = false; }
		}
		io.KeyCtrl  = mods & InputModBits::Control;
		io.KeyShift = mods & InputModBits::Shift;
		io.KeyAlt   = mods & InputModBits::Alt;
	};
	Input::OnButton += [this](MouseButton button, InputAction action, InputMods mods) {
		ImGuiIO& io = ImGui::GetIO();

		if (action == InputAction::Press && int(button) < ImGuiMouseButton_COUNT) { _mouseJustPressed[int(button)] = true; }
	};
	Input::OnMoved += [this](const glm::dvec2& pos) {
		ImGuiIO& io = ImGui::GetIO();

		io.MousePos = ImVec2(pos.x, pos.y);
	};
	Input::OnScroll += [this](const glm::dvec2& scroll) {
		ImGuiIO& io = ImGui::GetIO();

		io.MouseWheelH += scroll.x;
		io.MouseWheel += scroll.y;
	};
}

ImGuiRenderer::~ImGuiRenderer() noexcept {}

void ImGuiRenderer::BeginFrame() {
	ImGuiIO& io = ImGui::GetIO();

	// Update display size and platform data.
	{
		const auto windowSize      = _wsi.GetWindowSize();
		const auto framebufferSize = _wsi.GetFramebufferSize();
		io.DisplaySize             = ImVec2(windowSize.x, windowSize.y);
		if (windowSize.x > 0 && windowSize.y > 0) {
			io.DisplayFramebufferScale =
				ImVec2(float(framebufferSize.x) / float(windowSize.x), float(framebufferSize.y) / float(windowSize.y));
		}

		for (int i = 0; i < ImGuiMouseButton_COUNT; ++i) {
			io.MouseDown[i]      = _mouseJustPressed[i] || Input::GetButton(MouseButton(i));
			_mouseJustPressed[i] = false;
		}

		static auto lastTime = glfwGetTime();
		const auto now       = glfwGetTime();
		io.DeltaTime         = lastTime > 0.0 ? (float) (now - lastTime) : 1.0f / 60.0f;
		lastTime             = now;
	}

	ImGui::NewFrame();
}

void ImGuiRenderer::Render(Vulkan::CommandBufferHandle& cmd, uint32_t frameIndex) {
	ImGui::EndFrame();

	auto& device = _wsi.GetDevice();

	ImGui::Render();
	ImDrawData* drawData = ImGui::GetDrawData();

	// Determine our window size and ensure we don't render to a minimized screen.
	const int fbWidth  = static_cast<int>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
	const int fbHeight = static_cast<int>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
	if (fbWidth <= 0 || fbHeight <= 0) { return; }

	if (_vertexBuffers.size() <= frameIndex) { _vertexBuffers.resize(frameIndex + 1); }
	if (_indexBuffers.size() <= frameIndex) { _indexBuffers.resize(frameIndex + 1); }
	auto& vertexBuffer = _vertexBuffers[frameIndex];
	auto& indexBuffer  = _indexBuffers[frameIndex];

	// Set up our vertex and index buffers.
	if (drawData->TotalVtxCount > 0) {
		// Resize or create our buffers if needed.
		const size_t vertexSize = drawData->TotalVtxCount * sizeof(ImDrawVert);
		const size_t indexSize  = drawData->TotalIdxCount * sizeof(ImDrawIdx);
		if (!vertexBuffer || vertexBuffer->GetCreateInfo().Size < vertexSize) {
			vertexBuffer = device.CreateBuffer(
				Vulkan::BufferCreateInfo(Vulkan::BufferDomain::Host, vertexSize, vk::BufferUsageFlagBits::eVertexBuffer));
		}
		if (!indexBuffer || indexBuffer->GetCreateInfo().Size < indexSize) {
			indexBuffer = device.CreateBuffer(
				Vulkan::BufferCreateInfo(Vulkan::BufferDomain::Host, vertexSize, vk::BufferUsageFlagBits::eIndexBuffer));
		}

		// Copy our vertex and index data.
		ImDrawVert* vertices = reinterpret_cast<ImDrawVert*>(vertexBuffer->Map());
		ImDrawIdx* indices   = reinterpret_cast<ImDrawIdx*>(indexBuffer->Map());
		for (int i = 0; i < drawData->CmdListsCount; ++i) {
			const ImDrawList* list = drawData->CmdLists[i];
			memcpy(vertices, list->VtxBuffer.Data, list->VtxBuffer.Size * sizeof(ImDrawVert));
			memcpy(indices, list->IdxBuffer.Data, list->IdxBuffer.Size * sizeof(ImDrawIdx));
			vertices += list->VtxBuffer.Size;
			indices += list->IdxBuffer.Size;
		}
	}

	// Set up our render state.
	{
		Vulkan::RenderPassInfo rp = device.GetStockRenderPass(Vulkan::StockRenderPass::ColorOnly);
		rp.ClearAttachments       = 0;
		rp.LoadAttachments        = 1 << 0;
		cmd->BeginRenderPass(rp);
		SetRenderState(cmd, drawData, frameIndex);
	}

	const ImVec2 clipOffset = drawData->DisplayPos;
	const ImVec2 clipScale  = drawData->FramebufferScale;

	// Render command lists.
	{
		size_t globalVtxOffset = 0;
		size_t globalIdxOffset = 0;
		for (int i = 0; i < drawData->CmdListsCount; ++i) {
			const ImDrawList* cmdList = drawData->CmdLists[i];
			for (int j = 0; j < cmdList->CmdBuffer.Size; ++j) {
				const ImDrawCmd& drawCmd = cmdList->CmdBuffer[j];

				if (drawCmd.UserCallback != nullptr) {
					if (drawCmd.UserCallback == ImDrawCallback_ResetRenderState) {
						SetRenderState(cmd, drawData, frameIndex);
					} else {
						drawCmd.UserCallback(cmdList, &drawCmd);
					}
				} else {
					const ImVec2 clipMin(std::max((drawCmd.ClipRect.x - clipOffset.x) * clipScale.x, 0.0f),
					                     std::max((drawCmd.ClipRect.y - clipOffset.y) * clipScale.y, 0.0f));
					const ImVec2 clipMax(
						std::min((drawCmd.ClipRect.z - clipOffset.x) * clipScale.x, static_cast<float>(fbWidth)),
						std::min((drawCmd.ClipRect.w - clipOffset.y) * clipScale.y, static_cast<float>(fbHeight)));
					if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y) { continue; }

					const vk::Rect2D scissor(
						{static_cast<int32_t>(clipMin.x), static_cast<int32_t>(clipMin.y)},
						{static_cast<uint32_t>(clipMax.x - clipMin.x), static_cast<uint32_t>(clipMax.y - clipMin.y)});
					cmd->SetScissor(scissor);

					if (drawCmd.TextureId == 0) {
						cmd->SetTexture(0, 0, _fontTexture->GetView(), _fontSampler);
					} else {
						Vulkan::ImageView* view = reinterpret_cast<Vulkan::ImageView*>(drawCmd.TextureId);
						cmd->SetTexture(0, 0, *view, Vulkan::StockSampler::LinearClamp);
					}

					const float scaleX    = 2.0f / drawData->DisplaySize.x;
					const float scaleY    = 2.0f / drawData->DisplaySize.y;
					const PushConstant pc = {.ScaleX     = scaleX,
					                         .ScaleY     = scaleY,
					                         .TranslateX = -1.0f - drawData->DisplayPos.x * scaleX,
					                         .TranslateY = -1.0f - drawData->DisplayPos.y * scaleY};
					cmd->PushConstants(&pc, 0, sizeof(pc));

					cmd->DrawIndexed(
						drawCmd.ElemCount, 1, drawCmd.IdxOffset + globalIdxOffset, drawCmd.VtxOffset + globalVtxOffset, 0);
				}
			}
			globalVtxOffset += cmdList->VtxBuffer.Size;
			globalIdxOffset += cmdList->IdxBuffer.Size;
		}
	}

	cmd->EndRenderPass();
}

void ImGuiRenderer::SetRenderState(Vulkan::CommandBufferHandle& cmd, ImDrawData* drawData, uint32_t frameIndex) const {
	if (drawData->TotalVtxCount == 0) { return; }

	cmd->SetProgram(_program);
	cmd->SetTransparentSpriteState();
	cmd->SetVertexAttribute(0, 0, vk::Format::eR32G32Sfloat, offsetof(ImDrawVert, pos));
	cmd->SetVertexAttribute(1, 0, vk::Format::eR32G32Sfloat, offsetof(ImDrawVert, uv));
	cmd->SetVertexAttribute(2, 0, vk::Format::eR8G8B8A8Unorm, offsetof(ImDrawVert, col));
	cmd->SetVertexBinding(0, *_vertexBuffers[frameIndex], 0, sizeof(ImDrawVert), vk::VertexInputRate::eVertex);
	cmd->SetIndexBuffer(
		*_indexBuffers[frameIndex], 0, sizeof(ImDrawIdx) == 2 ? vk::IndexType::eUint16 : vk::IndexType::eUint32);
}
