#include <Luna/Renderer/GlslCompiler.hpp>
#include <Luna/Renderer/RenderContext.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/DescriptorSet.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/Sampler.hpp>

namespace Luna {
static bool LoadGraphicsShader(Vulkan::Device& device,
                               const Path& vertex,
                               const Path& fragment,
                               Vulkan::ShaderProgramVariant*& program) {
	auto& shaderManager = device.GetShaderManager();

	auto* shaderProgram = shaderManager.RegisterGraphics(vertex, fragment);
	auto* shaderVariant = shaderProgram->RegisterVariant();

	program = shaderVariant;

	return program->GetProgram() != nullptr;
}

static bool LoadComputeShader(Vulkan::Device& device, const Path& compute, Vulkan::ShaderProgramVariant*& program) {
	auto& shaderManager = device.GetShaderManager();

	auto* shaderProgram = shaderManager.RegisterCompute(compute);
	auto* shaderVariant = shaderProgram->RegisterVariant();

	program = shaderVariant;

	return program->GetProgram() != nullptr;
}

RenderContext::RenderContext(Vulkan::Device& device) : _device(device), _bindless(_device) {
	CreateDefaultImages();
	ReloadShaders();
}

const uint32_t RenderContext::GetFrameContextCount() const {
	return _device.GetFramesInFlight();
}

void RenderContext::BeginFrame(uint32_t frameIndex) {
	_frameIndex = frameIndex;
	_bindless.BeginFrame();
}

void RenderContext::CreateDefaultImages() {
	constexpr uint32_t width    = 1;
	constexpr uint32_t height   = 1;
	constexpr size_t pixelCount = width * height;
	uint32_t pixels[pixelCount];
	Vulkan::ImageInitialData initialImages[6];
	for (int i = 0; i < 6; ++i) { initialImages[i] = Vulkan::ImageInitialData{.Data = &pixels}; }
	const Vulkan::ImageCreateInfo imageCI2D = {
		.Domain        = Vulkan::ImageDomain::Physical,
		.Width         = width,
		.Height        = height,
		.Depth         = 1,
		.MipLevels     = 1,
		.ArrayLayers   = 1,
		.Format        = vk::Format::eR8G8B8A8Unorm,
		.InitialLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
		.Type          = vk::ImageType::e2D,
		.Usage         = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eInputAttachment,
		.Samples       = vk::SampleCountFlagBits::e1,
	};

	std::fill(pixels, pixels + pixelCount, 0xff000000);
	_defaultImages.Black2D = _device.CreateImage(imageCI2D, initialImages);

	std::fill(pixels, pixels + pixelCount, 0xff808080);
	_defaultImages.Gray2D = _device.CreateImage(imageCI2D, initialImages);

	std::fill(pixels, pixels + pixelCount, 0xffff8080);
	_defaultImages.Normal2D = _device.CreateImage(imageCI2D, initialImages);

	std::fill(pixels, pixels + pixelCount, 0xffffffff);
	_defaultImages.White2D = _device.CreateImage(imageCI2D, initialImages);
}

void RenderContext::ReloadShaders() {
	if (!LoadGraphicsShader(_device,
	                        "res://Shaders/Fullscreen.vert.glsl",
	                        "res://Shaders/BloomDownsample.frag.glsl",
	                        _shaders.BloomDownsample)) {
		return;
	}
	if (!LoadGraphicsShader(_device,
	                        "res://Shaders/Fullscreen.vert.glsl",
	                        "res://Shaders/BloomThreshold.frag.glsl",
	                        _shaders.BloomThreshold)) {
		return;
	}
	if (!LoadGraphicsShader(_device,
	                        "res://Shaders/Fullscreen.vert.glsl",
	                        "res://Shaders/BloomUpsample.frag.glsl",
	                        _shaders.BloomUpsample)) {
		return;
	}
	if (!LoadComputeShader(_device, "res://Shaders/Luminance.comp.glsl", _shaders.Luminance)) { return; }
	if (!LoadGraphicsShader(
				_device, "res://Shaders/PBRForward.vert.glsl", "res://Shaders/PBRForward.frag.glsl", _shaders.PBRForward)) {
		return;
	}
	if (!LoadGraphicsShader(
				_device, "res://Shaders/PBRGBuffer.vert.glsl", "res://Shaders/PBRGBuffer.frag.glsl", _shaders.PBRGBuffer)) {
		return;
	}
	if (!LoadGraphicsShader(
				_device, "res://Shaders/Fullscreen.vert.glsl", "res://Shaders/PBRDeferred.frag.glsl", _shaders.PBRDeferred)) {
		return;
	}
	if (!LoadGraphicsShader(
				_device, "res://Shaders/Fullscreen.vert.glsl", "res://Shaders/Tonemap.frag.glsl", _shaders.Tonemap)) {
		return;
	}
	if (!LoadGraphicsShader(
				_device, "res://Shaders/Visibility.vert.glsl", "res://Shaders/Visibility.frag.glsl", _shaders.Visibility)) {
		return;
	}
	if (!LoadGraphicsShader(
				_device, "res://Shaders/Fullscreen.vert.glsl", "res://Shaders/VisDebug.frag.glsl", _shaders.VisibilityDebug)) {
		return;
	}

	Log::Info("RenderContext", "Shaders reloaded.");
}

void RenderContext::SetCamera(const glm::mat4& projection, const glm::mat4& view) {
	_camera.Projection        = projection;
	_camera.View              = view;
	_camera.ViewProjection    = _camera.Projection * _camera.View;
	_camera.InvProjection     = glm::inverse(_camera.Projection);
	_camera.InvView           = glm::inverse(_camera.View);
	_camera.InvViewProjection = glm::inverse(_camera.ViewProjection);

	_frustum.BuildPlanes(_camera.InvViewProjection);

	glm::mat4 localView            = view;
	localView[3][0]                = 0;
	localView[3][1]                = 0;
	localView[3][2]                = 0;
	_camera.LocalViewProjection    = _camera.Projection * localView;
	_camera.InvLocalViewProjection = glm::inverse(_camera.LocalViewProjection);

	_camera.CameraRight    = _camera.InvView[0];
	_camera.CameraUp       = _camera.InvView[1];
	_camera.CameraFront    = -_camera.InvView[2];
	_camera.CameraPosition = _camera.InvView[3];

	glm::mat2 invZW(glm::vec2(_camera.InvProjection[2].z, _camera.InvProjection[2].w),
	                glm::vec2(_camera.InvProjection[3].z, _camera.InvProjection[3].w));
	const auto Project = [](const glm::vec2& zw) -> float { return -zw.x / zw.y; };
	_camera.ZNear      = Project(invZW * glm::vec2(0.0f, 1.0f));
	_camera.ZFar       = Project(invZW * glm::vec2(1.0f, 1.0f));
}

uint32_t RenderContext::SetTexture(const Vulkan::ImageView& view, const Vulkan::Sampler& sampler) {
	return _bindless.Texture(view, sampler);
}

uint32_t RenderContext::SetTexture(const Vulkan::ImageView& view, Vulkan::StockSampler sampler) {
	return _bindless.Texture(view, sampler);
}

uint32_t RenderContext::SetSrgbTexture(const Vulkan::ImageView& view, const Vulkan::Sampler& sampler) {
	return _bindless.SrgbTexture(view, sampler);
}

uint32_t RenderContext::SetSrgbTexture(const Vulkan::ImageView& view, Vulkan::StockSampler sampler) {
	return _bindless.SrgbTexture(view, sampler);
}

uint32_t RenderContext::SetUnormTexture(const Vulkan::ImageView& view, const Vulkan::Sampler& sampler) {
	return _bindless.UnormTexture(view, sampler);
}

uint32_t RenderContext::SetUnormTexture(const Vulkan::ImageView& view, Vulkan::StockSampler sampler) {
	return _bindless.UnormTexture(view, sampler);
}
}  // namespace Luna
