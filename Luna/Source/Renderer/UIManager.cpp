#include <imgui.h>

#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Filesystem.hpp>
#include <Luna/Core/Input.hpp>
#include <Luna/Core/Window.hpp>
#include <Luna/Renderer/IconsFontAwesome6.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Renderer/ShaderCompiler.hpp>
#include <Luna/Renderer/UIManager.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/Sampler.hpp>

namespace Luna {
enum class ImGuiSampleMode : uint32_t { Standard = 0, ImGuiFont = 1, Grayscale = 2 };

struct PushConstant {
	float ScaleX;
	float ScaleY;
	float TranslateX;
	float TranslateY;
	ImGuiSampleMode SampleMode;
};

static struct UIState {
	std::vector<Vulkan::BufferHandle> Buffers;
	Vulkan::ImageHandle FontImage;
	Vulkan::SamplerHandle FontSampler;
	Vulkan::Program* Program = nullptr;
} State;

static ImGuiKey ImGuiKeyMap(Key key) {
#define K(Ours, Theirs) \
	case Key::Ours:       \
		return ImGuiKey_##Theirs

	switch (key) {
		K(Tab, Tab);
		K(Left, LeftArrow);
		K(Right, RightArrow);
		K(Up, UpArrow);
		K(Down, DownArrow);
		K(PageUp, PageUp);
		K(PageDown, PageDown);
		K(Home, Home);
		K(End, End);
		K(Insert, Insert);
		K(Delete, Delete);
		K(Backspace, Backspace);
		K(Space, Space);
		K(Enter, Enter);
		K(Escape, Escape);
		K(Apostrophe, Apostrophe);
		K(Comma, Comma);
		K(Minus, Minus);
		K(Period, Period);
		K(Slash, Slash);
		K(Semicolon, Semicolon);
		K(Equal, Equal);
		K(LeftBracket, LeftBracket);
		K(Backslash, Backslash);
		K(RightBracket, RightBracket);
		K(GraveAccent, GraveAccent);
		K(CapsLock, CapsLock);
		K(ScrollLock, ScrollLock);
		K(NumLock, NumLock);
		K(PrintScreen, PrintScreen);
		K(Pause, Pause);
		K(Numpad0, Keypad0);
		K(Numpad1, Keypad1);
		K(Numpad2, Keypad2);
		K(Numpad3, Keypad3);
		K(Numpad4, Keypad4);
		K(Numpad5, Keypad5);
		K(Numpad6, Keypad6);
		K(Numpad7, Keypad7);
		K(Numpad8, Keypad8);
		K(Numpad9, Keypad9);
		K(NumpadDecimal, KeypadDecimal);
		K(NumpadDivide, KeypadDivide);
		K(NumpadMultiply, KeypadMultiply);
		K(NumpadSubtract, KeypadSubtract);
		K(NumpadAdd, KeypadAdd);
		K(NumpadEnter, KeypadEnter);
		K(NumpadEqual, KeypadEqual);
		K(ShiftLeft, LeftShift);
		K(ControlLeft, LeftCtrl);
		K(AltLeft, LeftAlt);
		K(ShiftRight, RightShift);
		K(ControlRight, RightCtrl);
		K(AltRight, RightAlt);
		K(Menu, Menu);
		K(_0, 0);
		K(_1, 1);
		K(_2, 2);
		K(_3, 3);
		K(_4, 4);
		K(_5, 5);
		K(_6, 6);
		K(_7, 7);
		K(_8, 8);
		K(_9, 9);
		K(A, A);
		K(B, B);
		K(C, C);
		K(D, D);
		K(E, E);
		K(F, F);
		K(G, G);
		K(H, H);
		K(I, I);
		K(J, J);
		K(K, K);
		K(L, L);
		K(M, M);
		K(N, N);
		K(O, O);
		K(P, P);
		K(Q, Q);
		K(R, R);
		K(S, S);
		K(T, T);
		K(U, U);
		K(V, V);
		K(W, W);
		K(X, X);
		K(Y, Y);
		K(Z, Z);
		K(F1, F1);
		K(F2, F2);
		K(F3, F3);
		K(F4, F4);
		K(F5, F5);
		K(F6, F6);
		K(F7, F7);
		K(F8, F8);
		K(F9, F9);
		K(F10, F10);
		K(F11, F11);
		K(F12, F12);
		K(F13, F13);
		K(F14, F14);
		K(F15, F15);
		K(F16, F16);
		K(F17, F17);
		K(F18, F18);
		K(F19, F19);
		K(F20, F20);
		K(F21, F21);
		K(F22, F22);
		K(F23, F23);
		K(F24, F24);
		default:
			return ImGuiKey_None;
	}

#undef K
}

bool UIManager::Initialize() {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();

	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	// Custom theming
	{
		ImGuiStyle& style = ImGui::GetStyle();

		// Main
		style.WindowPadding   = ImVec2(8.0f, 8.0f);
		style.FramePadding    = ImVec2(5.0f, 3.0f);
		style.CellPadding     = ImVec2(4.0f, 2.0f);
		style.FrameBorderSize = 1.0f;

		// Rounding
		style.WindowRounding    = 8.0f;
		style.ChildRounding     = 8.0f;
		style.FrameRounding     = 8.0f;
		style.PopupRounding     = 2.0f;
		style.ScrollbarRounding = 12.0f;
		style.GrabRounding      = 0.0f;
		style.LogSliderDeadzone = 4.0f;
		style.TabRounding       = 4.0f;

		// Fonts
		{
			io.Fonts->Clear();

			ImFontConfig jpConfig;
			jpConfig.MergeMode = true;

			ImFontConfig faConfig;
			faConfig.MergeMode      = true;
			faConfig.PixelSnapH     = true;
			const ImWchar faRange[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};

			auto roboto = Filesystem::OpenReadOnlyMapping("res://Fonts/Roboto-SemiMedium.ttf");
			if (roboto) { io.Fonts->AddFontFromMemoryTTF(roboto->MutableData(), roboto->GetSize(), 16.0f); }

			auto notoSans = Filesystem::OpenReadOnlyMapping("res://Fonts/NotoSansJP-Medium.otf");
			if (notoSans) {
				io.Fonts->AddFontFromMemoryTTF(
					notoSans->MutableData(), notoSans->GetSize(), 18.0f, &jpConfig, io.Fonts->GetGlyphRangesJapanese());
			}

			auto faRegular = Filesystem::OpenReadOnlyMapping("res://Fonts/FontAwesome6Free-Regular-400.otf");
			auto faSolid   = Filesystem::OpenReadOnlyMapping("res://Fonts/FontAwesome6Free-Solid-900.otf");
			if (faRegular && faSolid) {
				io.Fonts->AddFontFromMemoryTTF(faRegular->MutableData(), faRegular->GetSize(), 16.0f, &faConfig, faRange);
				io.Fonts->AddFontFromMemoryTTF(faSolid->MutableData(), faSolid->GetSize(), 16.0f, &faConfig, faRange);
			}
			UpdateFontAtlas();
		}

		// Colors
		ImVec4* colors                         = style.Colors;
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
	}

	Input::OnChar += [](int c) {
		ImGuiIO& io = ImGui::GetIO();
		io.AddInputCharacter(c);
	};
	Input::OnKey += [](Key key, InputAction action, InputMods mods) {
		if (action == InputAction::Repeat) { return; }
		ImGuiIO& io = ImGui::GetIO();
		io.AddKeyEvent(ImGuiMod_Ctrl, mods & InputModBits::Control);
		io.AddKeyEvent(ImGuiMod_Shift, mods & InputModBits::Shift);
		io.AddKeyEvent(ImGuiMod_Alt, mods & InputModBits::Alt);
		io.AddKeyEvent(ImGuiKeyMap(key), action == InputAction::Press);
	};
	Input::OnMouseButton += [](MouseButton button, InputAction action, InputMods mods) {
		ImGuiIO& io = ImGui::GetIO();
		io.AddMouseButtonEvent(int(button), action == InputAction::Press);
	};
	Input::OnMouseMoved += [](const glm::dvec2& pos) {
		ImGuiIO& io = ImGui::GetIO();
		if (!Input::GetCursorHidden()) { io.AddMousePosEvent(pos.x, pos.y); }
	};
	Input::OnMouseScrolled += [](const glm::dvec2& scroll) {
		ImGuiIO& io = ImGui::GetIO();
		io.AddMouseWheelEvent(scroll.x, scroll.y);
	};

	io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

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
	State.FontSampler = Renderer::GetDevice().CreateSampler(samplerCI);

	ShaderCompiler shaderCompiler;
	std::string shaderError;

	shaderCompiler.SetSourceFromFile("file://ImGui.vert.glsl", Vulkan::ShaderStage::Vertex);
	shaderCompiler.Preprocess();
	auto vertexCode = shaderCompiler.Compile(shaderError);
	shaderCompiler.SetSourceFromFile("file://ImGui.frag.glsl", Vulkan::ShaderStage::Fragment);
	shaderCompiler.Preprocess();
	auto fragmentCode = shaderCompiler.Compile(shaderError);

	State.Program = Renderer::GetDevice().RequestProgram(vertexCode, fragmentCode);

	return true;
}

void UIManager::Shutdown() {
	State.FontSampler.Reset();
	State.FontImage.Reset();
	for (auto& buffer : State.Buffers) { buffer.Reset(); }
	State.Buffers.clear();
}

void UIManager::BeginFrame() {
	ImGuiIO& io = ImGui::GetIO();

	const auto fbSize     = Engine::GetMainWindow()->GetFramebufferSize();
	const auto windowSize = Engine::GetMainWindow()->GetWindowSize();
	io.DisplaySize        = ImVec2(windowSize.x, windowSize.y);
	if (windowSize.x > 0 && windowSize.y > 0) {
		io.DisplayFramebufferScale = ImVec2(float(fbSize.x) / float(windowSize.x), float(fbSize.y) / float(windowSize.y));
	}

	auto cursor = ImGui::GetMouseCursor();
	switch (cursor) {
		case ImGuiMouseCursor_None:
		case ImGuiMouseCursor_Arrow:
		default:
			Input::SetCursorShape(MouseCursor::Arrow);
			break;
		case ImGuiMouseCursor_TextInput:
			Input::SetCursorShape(MouseCursor::IBeam);
			break;
		case ImGuiMouseCursor_ResizeAll:
			Input::SetCursorShape(MouseCursor::ResizeAll);
			break;
		case ImGuiMouseCursor_ResizeNS:
			Input::SetCursorShape(MouseCursor::ResizeNS);
			break;
		case ImGuiMouseCursor_ResizeEW:
			Input::SetCursorShape(MouseCursor::ResizeEW);
			break;
		case ImGuiMouseCursor_ResizeNESW:
			Input::SetCursorShape(MouseCursor::ResizeNESW);
			break;
		case ImGuiMouseCursor_ResizeNWSE:
			Input::SetCursorShape(MouseCursor::ResizeNWSE);
			break;
		case ImGuiMouseCursor_Hand:
			Input::SetCursorShape(MouseCursor::Hand);
			break;
	}

	ImGui::NewFrame();
}

void UIManager::Render(Vulkan::CommandBuffer& cmd) {
	const uint32_t frameIndex = Renderer::GetDevice().GetFrameIndex();

	ImGui::Render();
	ImDrawData* drawData = ImGui::GetDrawData();
	if (drawData->CmdListsCount == 0 || drawData->TotalVtxCount == 0 || drawData->TotalIdxCount == 0) { return; }

	// Determine our window size and ensure we don't render to a minimized screen.
	const int fbWidth  = static_cast<int>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
	const int fbHeight = static_cast<int>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
	if (fbWidth <= 0 || fbHeight <= 0) { return; }

	constexpr static vk::IndexType indexType = sizeof(ImDrawIdx) == 2 ? vk::IndexType::eUint16 : vk::IndexType::eUint32;

	const vk::DeviceSize vertexSize = drawData->TotalVtxCount * sizeof(ImDrawVert);
	const vk::DeviceSize indexSize  = drawData->TotalIdxCount * sizeof(ImDrawIdx);
	const vk::DeviceSize bufferSize = vertexSize + indexSize;
	if (frameIndex >= State.Buffers.size()) { State.Buffers.resize(frameIndex + 1); }
	auto& buffer = State.Buffers[frameIndex];
	if (!buffer || buffer->GetCreateInfo().Size < bufferSize) {
		const Vulkan::BufferCreateInfo bufferCI(
			Vulkan::BufferDomain::Host,
			bufferSize,
			vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer);
		buffer = Renderer::GetDevice().CreateBuffer(bufferCI);
	}
	uint8_t* bufferData  = static_cast<uint8_t*>(buffer->Map());
	ImDrawVert* vertices = reinterpret_cast<ImDrawVert*>(bufferData);
	ImDrawIdx* indices   = reinterpret_cast<ImDrawIdx*>(bufferData + vertexSize);
	for (int i = 0; i < drawData->CmdListsCount; ++i) {
		const ImDrawList* list = drawData->CmdLists[i];
		memcpy(vertices, list->VtxBuffer.Data, list->VtxBuffer.Size * sizeof(ImDrawVert));
		memcpy(indices, list->IdxBuffer.Data, list->IdxBuffer.Size * sizeof(ImDrawIdx));
		vertices += list->VtxBuffer.Size;
		indices += list->IdxBuffer.Size;
	}

	const ImVec2 clipOffset = drawData->DisplayPos;
	const ImVec2 clipScale  = drawData->FramebufferScale;
	const float scaleX      = 2.0f / drawData->DisplaySize.x;
	const float scaleY      = 2.0f / drawData->DisplaySize.y;
	PushConstant pc         = {.ScaleX     = scaleX,
	                           .ScaleY     = scaleY,
	                           .TranslateX = -1.0f - drawData->DisplayPos.x * scaleX,
	                           .TranslateY = -1.0f - drawData->DisplayPos.y * scaleY,
	                           .SampleMode = ImGuiSampleMode::Standard};

	const auto SetRenderState = [&cmd, &buffer, vertexSize]() {
		cmd.SetProgram(State.Program);
		cmd.SetTransparentSpriteState();
		cmd.SetCullMode(vk::CullModeFlagBits::eNone);
		cmd.SetVertexBinding(0, *buffer, 0, sizeof(ImDrawVert), vk::VertexInputRate::eVertex);
		cmd.SetVertexAttribute(0, 0, vk::Format::eR32G32Sfloat, offsetof(ImDrawVert, pos));
		cmd.SetVertexAttribute(1, 0, vk::Format::eR32G32Sfloat, offsetof(ImDrawVert, uv));
		cmd.SetVertexAttribute(2, 0, vk::Format::eR8G8B8A8Unorm, offsetof(ImDrawVert, col));
		cmd.SetIndexBuffer(*buffer, vertexSize, indexType);
	};
	SetRenderState();

	size_t globalVtxOffset = 0;
	size_t globalIdxOffset = 0;
	for (int i = 0; i < drawData->CmdListsCount; ++i) {
		const ImDrawList* cmdList = drawData->CmdLists[i];
		for (int j = 0; j < cmdList->CmdBuffer.Size; ++j) {
			const ImDrawCmd& drawCmd = cmdList->CmdBuffer[j];

			if (drawCmd.UserCallback != nullptr) {
				if (drawCmd.UserCallback == ImDrawCallback_ResetRenderState) {
					SetRenderState();
				} else {
					drawCmd.UserCallback(cmdList, &drawCmd);
				}
			} else {
				const ImVec2 clipMin(std::max((drawCmd.ClipRect.x - clipOffset.x) * clipScale.x, 0.0f),
				                     std::max((drawCmd.ClipRect.y - clipOffset.y) * clipScale.y, 0.0f));
				const ImVec2 clipMax(std::min((drawCmd.ClipRect.z - clipOffset.x) * clipScale.x, float(fbWidth)),
				                     std::min((drawCmd.ClipRect.w - clipOffset.y) * clipScale.y, float(fbHeight)));
				if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y) { continue; }

				const vk::Rect2D scissor({int32_t(clipMin.x), int32_t(clipMin.y)},
				                         {uint32_t(clipMax.x - clipMin.x), uint32_t(clipMax.y - clipMin.y)});
				cmd.SetScissor(scissor);

				pc.SampleMode = ImGuiSampleMode::Standard;
				if (drawCmd.TextureId == 0) {
					cmd.SetTexture(0, 0, State.FontImage->GetView(), *State.FontSampler);
					pc.SampleMode = ImGuiSampleMode::ImGuiFont;
				} else {
					/*
					UITexture* tex = static_cast<UITexture*>(drawCmd.TextureId);
					if (tex->SceneView >= 0) {
					  const auto& view = Renderer::GetSceneView(tex->SceneView);
					  if (Vulkan::FormatChannelCount(view.GetCreateInfo().Format) == 1) {
					    pc.SampleMode = ImGuiSampleMode::Grayscale;
					  }
					  cmd.SetTexture(0, 0, view, Vulkan::StockSampler::LinearClamp);
					} else {
					  if (Vulkan::FormatChannelCount(tex->View->GetCreateInfo().Format) == 1) {
					    pc.SampleMode = ImGuiSampleMode::Grayscale;
					  }
					  cmd.SetTexture(0, 0, *tex->View, Vulkan::StockSampler::LinearClamp);
					}
					*/
				}

				cmd.PushConstants(pc);

				cmd.DrawIndexed(
					drawCmd.ElemCount, 1, drawCmd.IdxOffset + globalIdxOffset, drawCmd.VtxOffset + globalVtxOffset, 0);
			}
		}
		globalVtxOffset += cmdList->VtxBuffer.Size;
		globalIdxOffset += cmdList->IdxBuffer.Size;
	}
}

void UIManager::UpdateFontAtlas() {
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Build();

	unsigned char* pixels = nullptr;
	int width, height;
	io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height);
	const Vulkan::ImageInitialData data(pixels);
	const auto imageCI =
		Vulkan::ImageCreateInfo::Immutable2D(vk::Format::eR8Unorm, vk::Extent2D(uint32_t(width), uint32_t(height)), false);
	State.FontImage = Renderer::GetDevice().CreateImage(imageCI, &data);
}
}  // namespace Luna
