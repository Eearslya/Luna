#include <imgui.h>

#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Input.hpp>
#include <Luna/Core/Window.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Renderer/ShaderManager.hpp>
#include <Luna/UI/IconsFontAwesome6.hpp>
#include <Luna/UI/UIManager.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/Sampler.hpp>
#include <Tracy/Tracy.hpp>

namespace Luna {
enum class ImGuiSampleMode : uint32_t { Standard = 0, ImGuiFont = 1, Grayscale = 2 };

struct PushConstant {
	float ScaleX;
	float ScaleY;
	float TranslateX;
	float TranslateY;
	ImGuiSampleMode SampleMode;
};

struct UITexture {
	const Vulkan::ImageView* View = nullptr;
	int32_t SceneView             = -1;
};

struct AlertState {
	bool Active = false;
	bool Opened = false;
	std::string Title;
	std::string Message;
};

struct DialogState {
	constexpr static const size_t MaxInputLength = 256;

	bool Active = false;
	bool Opened = false;
	std::string Title;
	std::string Value;
	std::function<void(bool, const std::string&)> Callback;
	char Buffer[MaxInputLength] = {0};
};

static struct UIState {
	std::vector<Vulkan::BufferHandle> Buffers;
	Vulkan::ImageHandle FontImage;
	Vulkan::SamplerHandle FontSampler;
	bool MouseJustPressed[16] = {false};
	Vulkan::Program* Program  = nullptr;
	std::array<UITexture, 64> Textures;
	uint32_t NextTexture = 0;
	AlertState AlertDialogState;
	DialogState TextDialogState;
} UIState;

bool UIManager::Initialize() {
	ZoneScopedN("UIManager::Initialize");

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();

	// Basic config flags.
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
		io.KeyMap[ImGuiKey_B]           = int(Key::B);
		io.KeyMap[ImGuiKey_C]           = int(Key::C);
		io.KeyMap[ImGuiKey_D]           = int(Key::D);
		io.KeyMap[ImGuiKey_E]           = int(Key::E);
		io.KeyMap[ImGuiKey_F]           = int(Key::F);
		io.KeyMap[ImGuiKey_G]           = int(Key::G);
		io.KeyMap[ImGuiKey_H]           = int(Key::H);
		io.KeyMap[ImGuiKey_I]           = int(Key::I);
		io.KeyMap[ImGuiKey_J]           = int(Key::J);
		io.KeyMap[ImGuiKey_K]           = int(Key::K);
		io.KeyMap[ImGuiKey_L]           = int(Key::L);
		io.KeyMap[ImGuiKey_M]           = int(Key::M);
		io.KeyMap[ImGuiKey_N]           = int(Key::N);
		io.KeyMap[ImGuiKey_O]           = int(Key::O);
		io.KeyMap[ImGuiKey_P]           = int(Key::P);
		io.KeyMap[ImGuiKey_Q]           = int(Key::Q);
		io.KeyMap[ImGuiKey_R]           = int(Key::R);
		io.KeyMap[ImGuiKey_S]           = int(Key::S);
		io.KeyMap[ImGuiKey_T]           = int(Key::T);
		io.KeyMap[ImGuiKey_U]           = int(Key::U);
		io.KeyMap[ImGuiKey_V]           = int(Key::V);
		io.KeyMap[ImGuiKey_W]           = int(Key::W);
		io.KeyMap[ImGuiKey_X]           = int(Key::X);
		io.KeyMap[ImGuiKey_Y]           = int(Key::Y);
		io.KeyMap[ImGuiKey_Z]           = int(Key::Z);
	}

	io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
	UIState.Program = ShaderManager::RegisterGraphics("res://Shaders/ImGui.vert.glsl", "res://Shaders/ImGui.frag.glsl")
	                    ->RegisterVariant()
	                    ->GetProgram();

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
	UIState.FontSampler = Renderer::GetDevice().CreateSampler(samplerCI);

	Input::OnChar += [](int c) {
		ImGuiIO& io = ImGui::GetIO();

		io.AddInputCharacter(c);
	};
	Input::OnKey += [](Key key, InputAction action, InputMods mods) {
		ImGuiIO& io = ImGui::GetIO();

		if (int(key) >= 0 && int(key) < 512) {
			if (action == InputAction::Press) { io.KeysDown[static_cast<int>(key)] = true; }
			if (action == InputAction::Release) { io.KeysDown[static_cast<int>(key)] = false; }
		}
		io.KeyCtrl  = mods & InputModBits::Control;
		io.KeyShift = mods & InputModBits::Shift;
		io.KeyAlt   = mods & InputModBits::Alt;
	};
	Input::OnMouseButton += [](MouseButton button, InputAction action, InputMods mods) {
		ImGuiIO& io = ImGui::GetIO();

		io.AddMouseButtonEvent(int(button), action == InputAction::Press);
		if (action == InputAction::Press && int(button) < ImGuiMouseButton_COUNT) {
			UIState.MouseJustPressed[int(button)] = true;
		}
	};
	Input::OnMouseMoved += [](const glm::dvec2& pos) {
		ImGuiIO& io = ImGui::GetIO();

		if (!Input::GetCursorHidden()) { io.MousePos = ImVec2(pos.x, pos.y); }
	};
	Input::OnMouseScrolled += [](const glm::dvec2& scroll) {
		ImGuiIO& io = ImGui::GetIO();

		io.AddMouseWheelEvent(scroll.x, scroll.y);
	};

	return true;
}

void UIManager::Shutdown() {
	ZoneScopedN("UIManager::Shutdown");

	UIState.FontSampler.Reset();
	UIState.FontImage.Reset();
	UIState.Buffers.clear();
	ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
}

void UIManager::Alert(const std::string& title, const std::string& message) {
	UIState.AlertDialogState.Active  = true;
	UIState.AlertDialogState.Opened  = true;
	UIState.AlertDialogState.Title   = title;
	UIState.AlertDialogState.Message = message;
}

void UIManager::BeginFrame(double deltaTime) {
	ZoneScopedN("UIManager::BeginFrame");

	ImGuiIO& io = ImGui::GetIO();

	// Update display size and platform data.
	{
		const auto fbSize     = Engine::GetMainWindow()->GetFramebufferSize();
		const auto windowSize = Engine::GetMainWindow()->GetWindowSize();
		io.DisplaySize        = ImVec2(windowSize.x, windowSize.y);
		if (windowSize.x > 0 && windowSize.y > 0) {
			io.DisplayFramebufferScale = ImVec2(float(fbSize.x) / float(windowSize.x), float(fbSize.y) / float(windowSize.y));
		}

		for (int i = 0; i < ImGuiMouseButton_COUNT; ++i) {
			io.MouseDown[i] = UIState.MouseJustPressed[i] || Input::GetButton(MouseButton(i)) == InputAction::Press;
			UIState.MouseJustPressed[i] = false;
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

		io.DeltaTime = deltaTime;
	}

	UIState.NextTexture = 0;
	ImGui::NewFrame();

	if (UIState.AlertDialogState.Active) {
		const std::string dialogId = fmt::format("{}##UIManagerAlertDialog", UIState.AlertDialogState.Title);

		if (UIState.AlertDialogState.Opened) {
			ImGui::OpenPopup(dialogId.c_str());
			UIState.AlertDialogState.Opened = false;
		}

		if (ImGui::BeginPopupModal(dialogId.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			bool close = false;

			ImGui::Text("%s", UIState.AlertDialogState.Message.c_str());

			if (ImGui::Button("OK")) { close = true; }

			if (close) {
				UIState.AlertDialogState.Active = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	if (UIState.TextDialogState.Active) {
		const std::string dialogId = fmt::format("{}##UIManagerTextDialog", UIState.TextDialogState.Title);

		if (UIState.TextDialogState.Opened) {
			ImGui::OpenPopup(dialogId.c_str());
			UIState.TextDialogState.Opened = false;
		}

		if (ImGui::BeginPopupModal(dialogId.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			bool submit = false;
			bool cancel = false;

			if (ImGui::InputText("##UIManagerTextDialogInput",
			                     UIState.TextDialogState.Buffer,
			                     DialogState::MaxInputLength,
			                     ImGuiInputTextFlags_EnterReturnsTrue)) {
				submit = true;
			}

			if (ImGui::Button("OK")) { submit = true; }
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) { cancel = true; }

			if (submit) {
				UIState.TextDialogState.Callback(true, UIState.TextDialogState.Buffer);
				UIState.TextDialogState.Active = false;
				ImGui::CloseCurrentPopup();
			} else if (cancel) {
				UIState.TextDialogState.Callback(false, "");
				UIState.TextDialogState.Active = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}
}

void UIManager::Render(Vulkan::CommandBuffer& cmd) {
	ZoneScopedN("UIManager::Render");

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
	if (frameIndex >= UIState.Buffers.size()) { UIState.Buffers.resize(frameIndex + 1); }
	auto& buffer = UIState.Buffers[frameIndex];
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
		cmd.SetProgram(UIState.Program);
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
					cmd.SetTexture(0, 0, UIState.FontImage->GetView(), *UIState.FontSampler);
					pc.SampleMode = ImGuiSampleMode::ImGuiFont;
				} else {
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
				}

				cmd.PushConstants(&pc, 0, sizeof(pc));

				cmd.DrawIndexed(
					drawCmd.ElemCount, 1, drawCmd.IdxOffset + globalIdxOffset, drawCmd.VtxOffset + globalVtxOffset, 0);
			}
		}
		globalVtxOffset += cmdList->VtxBuffer.Size;
		globalIdxOffset += cmdList->IdxBuffer.Size;
	}
}

ImTextureID UIManager::SceneView(int view) {
	auto& tex     = UIState.Textures[UIState.NextTexture++];
	tex.SceneView = view;

	return &tex;
}

void UIManager::TextDialog(const std::string& title,
                           std::function<void(bool, const std::string&)>&& callback,
                           const std::string& initialValue) {
	if (UIState.TextDialogState.Active) { return; }
	UIState.TextDialogState.Active   = true;
	UIState.TextDialogState.Opened   = true;
	UIState.TextDialogState.Value    = initialValue;
	UIState.TextDialogState.Callback = std::move(callback);
	UIState.TextDialogState.Title    = title;
	strncpy_s(UIState.TextDialogState.Buffer, initialValue.c_str(), DialogState::MaxInputLength);
}

ImTextureID UIManager::Texture(const Vulkan::ImageHandle& img) {
	auto& tex = UIState.Textures[UIState.NextTexture++];
	tex.View  = &img->GetView();

	return &tex;
}

void UIManager::UpdateFontAtlas() {
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Build();

	unsigned char* pixels = nullptr;
	int width, height;
	io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height);
	const Vulkan::ImageInitialData data{.Data = pixels};
	const auto imageCI = Vulkan::ImageCreateInfo::Immutable2D(
		vk::Format::eR8Unorm, static_cast<uint32_t>(width), static_cast<uint32_t>(height), false);
	UIState.FontImage = Renderer::GetDevice().CreateImage(imageCI, &data);
}
}  // namespace Luna
