#pragma once

#include <Luna/Utility/IntrusivePtr.hpp>
#include <functional>
#include <string>

using ImTextureID = void*;

namespace Luna {
namespace Vulkan {
class CommandBuffer;
class Image;

using ImageHandle = IntrusivePtr<Image>;
}  // namespace Vulkan

struct UITexture;

class UIManager {
 public:
	static bool Initialize();
	static void Shutdown();

	static void BeginFrame(double deltaTime);
	static void Render(Vulkan::CommandBuffer& cmd);
	static ImTextureID SceneView(int view);
	static void TextDialog(const std::string& title,
	                       std::function<void(bool, const std::string&)>&& callback,
	                       const std::string& initialValue = "");
	static ImTextureID Texture(const Vulkan::ImageHandle& img);
	static void UpdateFontAtlas();
};
}  // namespace Luna
