#include <imgui.h>

#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/ImGuiRenderer.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/RenderPass.hpp>
#include <Luna/Vulkan/Sampler.hpp>
#include <Luna/Vulkan/WSI.hpp>

namespace Luna {
namespace Vulkan {
ImGuiRenderer* ImGuiRenderer::_instance = nullptr;

enum class ImGuiSampleMode : uint32_t { Standard = 0, ImGuiFont = 1, Grayscale = 2 };

struct PushConstant {
	float ScaleX;
	float ScaleY;
	float TranslateX;
	float TranslateY;
	ImGuiSampleMode SampleMode;
};

ImGuiRenderer::ImGuiRenderer(WSI& wsi) : _wsi(wsi) {
	auto& device = wsi.GetDevice();
	_instance    = this;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();

	// Basic config flags.
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	// Custom theming
	{
		ImGuiStyle& style = ImGui::GetStyle();
		ImVec4* colors    = style.Colors;

		colors[ImGuiCol_Text]                  = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
		colors[ImGuiCol_TextDisabled]          = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
		colors[ImGuiCol_WindowBg]              = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
		colors[ImGuiCol_ChildBg]               = ImVec4(0.06f, 0.06f, 0.06f, 0.00f);
		colors[ImGuiCol_PopupBg]               = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
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
		io.KeyMap[ImGuiKey_Tab]         = int(Key::Tab);
		io.KeyMap[ImGuiKey_LeftArrow]   = int(Key::Left);
		io.KeyMap[ImGuiKey_RightArrow]  = int(Key::Right);
		io.KeyMap[ImGuiKey_UpArrow]     = int(Key::Up);
		io.KeyMap[ImGuiKey_DownArrow]   = int(Key::Down);
		io.KeyMap[ImGuiKey_PageUp]      = int(Key::PageUp);
		io.KeyMap[ImGuiKey_PageDown]    = int(Key::PageDown);
		io.KeyMap[ImGuiKey_Home]        = int(Key::Home);
		io.KeyMap[ImGuiKey_End]         = int(Key::End);
		io.KeyMap[ImGuiKey_Insert]      = int(Key::Insert);
		io.KeyMap[ImGuiKey_Delete]      = int(Key::Delete);
		io.KeyMap[ImGuiKey_Backspace]   = int(Key::Backspace);
		io.KeyMap[ImGuiKey_Space]       = int(Key::Space);
		io.KeyMap[ImGuiKey_Enter]       = int(Key::Enter);
		io.KeyMap[ImGuiKey_Escape]      = int(Key::Escape);
		io.KeyMap[ImGuiKey_KeyPadEnter] = int(Key::NumpadEnter);
		io.KeyMap[ImGuiKey_A]           = int(Key::A);
		io.KeyMap[ImGuiKey_C]           = int(Key::C);
		io.KeyMap[ImGuiKey_V]           = int(Key::V);
		io.KeyMap[ImGuiKey_X]           = int(Key::X);
		io.KeyMap[ImGuiKey_Y]           = int(Key::Y);
		io.KeyMap[ImGuiKey_Z]           = int(Key::Z);
	}

	// Renderer data
	{
		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

		constexpr static const char* vertGlsl = R"GLSL(
#version 450 core
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV0;
layout(location = 2) in vec4 inColor;
layout(push_constant) uniform PushConstant { vec2 Scale; vec2 Translate; uint SampleMode; } PC;
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
layout(push_constant) uniform PushConstant { vec2 Scale; vec2 Translate; uint SampleMode; } PC;
layout(set=0, binding=0) uniform sampler2D Texture;
layout(location = 0) out vec4 outColor;
void main() {
  vec4 texColor;
  switch(PC.SampleMode) {
   case 1: // ImGui Font
    texColor = vec4(1.0f, 1.0f, 1.0f, texture(Texture, In.UV.st).r);
    break;
   case 2: // Grayscale
    texColor.r = texture(Texture, In.UV.st).r;
    texColor = vec4(texColor.rrr, 1.0f);
    break;
   default: // Standard
    texColor = texture(Texture, In.UV.st);
    break;
  }
  outColor = In.Color * texColor;
}
)GLSL";

		_program = device.RequestProgram(vertGlsl, fragGlsl);

		UpdateFontAtlas();

		const SamplerCreateInfo samplerCI{.MagFilter        = vk::Filter::eLinear,
		                                  .MinFilter        = vk::Filter::eLinear,
		                                  .MipmapMode       = vk::SamplerMipmapMode::eLinear,
		                                  .AddressModeU     = vk::SamplerAddressMode::eRepeat,
		                                  .AddressModeV     = vk::SamplerAddressMode::eRepeat,
		                                  .AddressModeW     = vk::SamplerAddressMode::eRepeat,
		                                  .AnisotropyEnable = false,
		                                  .MaxAnisotropy    = 1.0f,
		                                  .MinLod           = -1000.0f,
		                                  .MaxLod           = 1000.0f};
		_fontSampler = device.CreateSampler(samplerCI);
	}

	Input::OnChar += [this](int c) {
		ImGuiIO& io = ImGui::GetIO();

		io.AddInputCharacter(c);
	};
	Input::OnKey += [this](Key key, InputAction action, InputMods mods) {
		ImGuiIO& io = ImGui::GetIO();

		if (int(key) >= 0 && int(key) < 512) {
			if (action == InputAction::Press) { io.KeysDown[static_cast<int>(key)] = true; }
			if (action == InputAction::Release) { io.KeysDown[static_cast<int>(key)] = false; }
		}
		io.KeyCtrl  = mods & InputModBits::Control;
		io.KeyShift = mods & InputModBits::Shift;
		io.KeyAlt   = mods & InputModBits::Alt;
	};
	Input::OnMouseButton += [this](MouseButton button, InputAction action, InputMods mods) {
		ImGuiIO& io = ImGui::GetIO();

		if (action == InputAction::Press && int(button) < ImGuiMouseButton_COUNT) { _mouseJustPressed[int(button)] = true; }
	};
	Input::OnMouseMoved += [this](const glm::dvec2& pos) {
		ImGuiIO& io = ImGui::GetIO();

		io.MousePos = ImVec2(pos.x, pos.y);
	};
	Input::OnMouseScrolled += [this](const glm::dvec2& scroll) {
		ImGuiIO& io = ImGui::GetIO();

		io.MouseWheelH += scroll.x;
		io.MouseWheel += scroll.y;
	};
}

ImGuiRenderer::~ImGuiRenderer() noexcept {
	_instance = nullptr;
}

void ImGuiRenderer::BeginFrame(const vk::Extent2D& fbSize) {
	ImGuiIO& io = ImGui::GetIO();

	_textures.BeginFrame();

	// Update display size and platform data.
	{
		const auto windowSize = _wsi.GetWindowSize();
		io.DisplaySize        = ImVec2(windowSize.x, windowSize.y);
		if (windowSize.x > 0 && windowSize.y > 0) {
			io.DisplayFramebufferScale =
				ImVec2(float(fbSize.width) / float(windowSize.x), float(fbSize.height) / float(windowSize.y));
		}

		for (int i = 0; i < ImGuiMouseButton_COUNT; ++i) {
			io.MouseDown[i]      = _mouseJustPressed[i] || Input::GetButton(MouseButton(i)) == InputAction::Press;
			_mouseJustPressed[i] = false;
		}

		static auto lastTime = _wsi.GetTime();
		const auto now       = _wsi.GetTime();
		io.DeltaTime         = lastTime > 0.0 ? (float) (now - lastTime) : 1.0f / 60.0f;
		lastTime             = now;
	}

	ImGui::NewFrame();
}

void ImGuiRenderer::Render(CommandBuffer& cmd, bool clear) {
	auto& device          = _wsi.GetDevice();
	const auto frameIndex = device.GetFrameIndex();

	ImGui::Render();
	ImDrawData* drawData = ImGui::GetDrawData();
	if (drawData->CmdListsCount == 0) { return; }

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
			vertexBuffer =
				device.CreateBuffer(BufferCreateInfo{BufferDomain::Host, vertexSize, vk::BufferUsageFlagBits::eVertexBuffer});
		}
		if (!indexBuffer || indexBuffer->GetCreateInfo().Size < indexSize) {
			indexBuffer =
				device.CreateBuffer(BufferCreateInfo{BufferDomain::Host, vertexSize, vk::BufferUsageFlagBits::eIndexBuffer});
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

	const ImVec2 clipOffset = drawData->DisplayPos;
	const ImVec2 clipScale  = drawData->FramebufferScale;

	// Render command lists.
	SetRenderState(cmd, drawData, frameIndex);
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
					cmd.SetScissor(scissor);

					ImGuiSampleMode sampleMode = ImGuiSampleMode::Standard;
					if (drawCmd.TextureId == 0) {
						cmd.SetTexture(0, 0, _fontTexture->GetView(), *_fontSampler);
						sampleMode = ImGuiSampleMode::ImGuiFont;
					} else {
						ImGuiTexture* texture = reinterpret_cast<ImGuiTexture*>(drawCmd.TextureId);
						cmd.SetTexture(0, 0, *texture->View, *texture->Sampler);

						if (FormatChannelCount(texture->View->GetCreateInfo().Format) == 1) {
							sampleMode = ImGuiSampleMode::Grayscale;
						}
					}

					const float scaleX    = 2.0f / drawData->DisplaySize.x;
					const float scaleY    = 2.0f / drawData->DisplaySize.y;
					const PushConstant pc = {.ScaleX     = scaleX,
					                         .ScaleY     = scaleY,
					                         .TranslateX = -1.0f - drawData->DisplayPos.x * scaleX,
					                         .TranslateY = -1.0f - drawData->DisplayPos.y * scaleY,
					                         .SampleMode = sampleMode};
					cmd.PushConstants(&pc, 0, sizeof(pc));

					cmd.DrawIndexed(
						drawCmd.ElemCount, 1, drawCmd.IdxOffset + globalIdxOffset, drawCmd.VtxOffset + globalVtxOffset, 0);
				}
			}
			globalVtxOffset += cmdList->VtxBuffer.Size;
			globalIdxOffset += cmdList->IdxBuffer.Size;
		}
	}
}

ImGuiTextureId ImGuiRenderer::Texture(ImageView& view, const Sampler& sampler, uint32_t arrayLayer) {
	Hasher h;
	h(view.GetCookie());
	h(sampler.GetCookie());
	h(arrayLayer);
	const auto hash = h.Get();

	auto* texture = _textures.Request(hash);
	if (texture) { return reinterpret_cast<ImGuiTextureId>(texture); }

	return reinterpret_cast<ImGuiTextureId>(_textures.Emplace(hash, view, sampler, arrayLayer));
}

ImGuiTextureId ImGuiRenderer::Texture(ImageView& view, StockSampler sampler, uint32_t arrayLayer) {
	return Texture(view, _wsi.GetDevice().GetStockSampler(sampler), arrayLayer);
}

void ImGuiRenderer::SetRenderState(CommandBuffer& cmd, ImDrawData* drawData, uint32_t frameIndex) const {
	assert(_program);
	assert(frameIndex < _vertexBuffers.size());
	assert(frameIndex < _indexBuffers.size());

	if (drawData->TotalVtxCount == 0) { return; }

	cmd.SetProgram(_program);
	cmd.SetTransparentSpriteState();
	cmd.SetCullMode(vk::CullModeFlagBits::eNone);
	cmd.SetVertexAttribute(0, 0, vk::Format::eR32G32Sfloat, offsetof(ImDrawVert, pos));
	cmd.SetVertexAttribute(1, 0, vk::Format::eR32G32Sfloat, offsetof(ImDrawVert, uv));
	cmd.SetVertexAttribute(2, 0, vk::Format::eR8G8B8A8Unorm, offsetof(ImDrawVert, col));
	cmd.SetVertexBinding(0, *_vertexBuffers[frameIndex], 0, sizeof(ImDrawVert), vk::VertexInputRate::eVertex);
	cmd.SetIndexBuffer(
		*_indexBuffers[frameIndex], 0, sizeof(ImDrawIdx) == 2 ? vk::IndexType::eUint16 : vk::IndexType::eUint32);
}

void ImGuiRenderer::BeginDockspace(bool background) {
	ImGuiIO& io       = ImGui::GetIO();
	ImGuiStyle& style = ImGui::GetStyle();

	ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;
	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
	                               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
	                               ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	if (!background) { windowFlags |= ImGuiWindowFlags_NoBackground; }

	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(viewport->Size);
	ImGui::SetNextWindowViewport(viewport->ID);

	if (!background) { ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0)); }
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::Begin("Dockspace", nullptr, windowFlags);
	ImGui::PopStyleVar(3);
	if (!background) { ImGui::PopStyleColor(); }

	ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(370.0f, 64.0f));
	ImGuiID dockspaceId = ImGui::GetID("Dockspace");
	ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);
	ImGui::PopStyleVar();
}

void ImGuiRenderer::EndDockspace() {
	ImGui::End();
}

void ImGuiRenderer::UpdateFontAtlas() {
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Build();

	unsigned char* pixels = nullptr;
	int width, height;
	io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height);
	const ImageInitialData data{.Data = pixels};
	const auto imageCI = ImageCreateInfo::Immutable2D(
		vk::Format::eR8Unorm, static_cast<uint32_t>(width), static_cast<uint32_t>(height), false);
	_fontTexture = _wsi.GetDevice().CreateImage(imageCI, &data);
}

void ImGuiRenderer::BuildRenderPass(Vulkan::CommandBuffer& cmd) {
	Render(cmd, false);
}

void ImGuiRenderer::EnqueuePrepareRenderPass(RenderGraph& graph, TaskComposer& composer) {
	const auto& fbSize = _wsi.GetFramebufferSize();
	BeginFrame(vk::Extent2D(fbSize.x, fbSize.y));
	if (_renderFunc) { _renderFunc(); }
}
}  // namespace Vulkan
}  // namespace Luna
