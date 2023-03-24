#include "SceneLoader.hpp"

#include <Luna/Platform/Filesystem.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Utility/Threading.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/Common.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/Sampler.hpp>
#include <fastgltf_parser.hpp>
#include <fastgltf_util.hpp>
#include <glm/glm.hpp>

template <class... Ts>
struct Overloaded : Ts... {
	using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

struct Buffer {
	uint32_t Index = 0;
	std::vector<uint8_t> Data;
};

struct Image {
	uint32_t Index = 0;
	Luna::Vulkan::ImageHandle Handle;
};

struct Sampler {
	Luna::Vulkan::Sampler* Handle = nullptr;
};

struct Texture {
	Image* Image     = nullptr;
	Sampler* Sampler = nullptr;
};

struct Material {
	glm::vec4 BaseColorFactor = glm::vec4(1, 1, 1, 1);
	glm::vec3 EmissiveFactor  = glm::vec3(0, 0, 0);
	Texture* Albedo           = nullptr;
	Texture* Normal           = nullptr;
	Texture* PBR              = nullptr;
	Texture* Occlusion        = nullptr;
	Texture* Emissive         = nullptr;
};

struct GltfContext {
	Luna::Vulkan::Device& Device;
	Luna::Scene& Scene;

	std::unique_ptr<fastgltf::Asset> Asset;
	Luna::Path GltfPath;
	Luna::Path GltfFolder;
	std::filesystem::path GltfFolderFs;

	std::vector<Buffer> Buffers;
	std::vector<Image> Images;
	std::vector<Material> Materials;
	std::vector<Sampler> Samplers;
	std::vector<Texture> Textures;

	Sampler* DefaultSampler = nullptr;
};

static Luna::Path GetPath(const GltfContext& context, const std::filesystem::path& relativePath) {
	const Luna::Path path = relativePath.lexically_relative(context.GltfFolderFs).string();

	return context.GltfFolder / path;
}

static void Parse(GltfContext& context) {
	auto* filesystem = Luna::Filesystem::Get();

	fastgltf::Parser parser(fastgltf::Extensions::KHR_mesh_quantization | fastgltf::Extensions::KHR_texture_transform);
	fastgltf::GltfDataBuffer gltfData;
	{
		auto gltfMapping = filesystem->OpenReadOnlyMapping(context.GltfPath);
		gltfData.copyBytes(gltfMapping->MutableData<uint8_t>(), gltfMapping->GetSize());
	}

	std::unique_ptr<fastgltf::glTF> loaded;
	fastgltf::GltfType gltfType = fastgltf::determineGltfFileType(&gltfData);
	if (gltfType == fastgltf::GltfType::Invalid) { throw std::runtime_error("Invalid glTF/GLB file!"); }

	const fastgltf::Options options = fastgltf::Options::LoadGLBBuffers | fastgltf::Options::DecomposeNodeMatrices;
	const auto gltfDir              = filesystem->GetFilesystemPath(context.GltfFolder);
	if (gltfType == fastgltf::GltfType::glTF) {
		loaded = parser.loadGLTF(&gltfData, gltfDir, options);
	} else {
		loaded = parser.loadBinaryGLTF(&gltfData, gltfDir, options);
	}

	fastgltf::Error loadError = fastgltf::Error::None;

	loadError = parser.getError();
	if (loadError != fastgltf::Error::None) {
		Luna::Log::Error("SceneLoader", "Failed to load glTF file: {}", int(loadError));
		return;
	}
	loadError = loaded->parse(fastgltf::Category::All);
	if (loadError != fastgltf::Error::None) {
		Luna::Log::Error("SceneLoader", "Failed to parse glTF file: {}", int(loadError));
		return;
	}
	loadError = loaded->validate();
	if (loadError != fastgltf::Error::None) {
		Luna::Log::Error("SceneLoader", "Failed to validate glTF file: {}", int(loadError));
	}

	context.Asset = loaded->getParsedAsset();
}

static void Preallocate(GltfContext& context) {
	if (!context.Asset) { return; }

	context.Buffers.resize(context.Asset->buffers.size());
	context.Images.resize(context.Asset->images.size());
	context.Samplers.resize(context.Asset->samplers.size() + 1);
	context.Textures.resize(context.Asset->textures.size());

	context.DefaultSampler = &context.Samplers.back();
}

static void ImportSamplers(GltfContext& context) {
	if (!context.Asset) { return; }

	auto& device              = context.Device;
	const auto& asset         = *context.Asset;
	const size_t samplerCount = asset.samplers.size();
	for (size_t i = 0; i < samplerCount; ++i) {
		const auto& gltfSampler = asset.samplers[i];
		auto& sampler           = context.Samplers[i];

		Luna::Vulkan::SamplerCreateInfo samplerCI{
			.MagFilter        = vk::Filter::eLinear,
			.MinFilter        = vk::Filter::eLinear,
			.MipmapMode       = vk::SamplerMipmapMode::eLinear,
			.AddressModeU     = vk::SamplerAddressMode::eRepeat,
			.AddressModeV     = vk::SamplerAddressMode::eRepeat,
			.AnisotropyEnable = device.GetDeviceInfo().EnabledFeatures.Core.samplerAnisotropy,
			.MaxAnisotropy    = device.GetDeviceInfo().Properties.Core.limits.maxSamplerAnisotropy,
			.MinLod           = 0.0f,
			.MaxLod           = 16.0f};

		if (gltfSampler.magFilter.has_value()) {
			switch (gltfSampler.magFilter.value()) {
				case fastgltf::Filter::Linear:
					samplerCI.MagFilter = vk::Filter::eLinear;
					break;

				case fastgltf::Filter::Nearest:
				default:
					samplerCI.MagFilter = vk::Filter::eNearest;
					break;
			}
		}
		if (gltfSampler.minFilter.has_value()) {
			switch (gltfSampler.minFilter.value()) {
				case fastgltf::Filter::LinearMipMapNearest:
				case fastgltf::Filter::Linear:
					samplerCI.MinFilter  = vk::Filter::eLinear;
					samplerCI.MipmapMode = vk::SamplerMipmapMode::eNearest;
					break;

				case fastgltf::Filter::LinearMipMapLinear:
					samplerCI.MinFilter  = vk::Filter::eLinear;
					samplerCI.MipmapMode = vk::SamplerMipmapMode::eLinear;
					break;

				case fastgltf::Filter::NearestMipMapLinear:
					samplerCI.MinFilter  = vk::Filter::eNearest;
					samplerCI.MipmapMode = vk::SamplerMipmapMode::eLinear;
					break;

				case fastgltf::Filter::NearestMipMapNearest:
				case fastgltf::Filter::Nearest:
				default:
					samplerCI.MinFilter  = vk::Filter::eNearest;
					samplerCI.MipmapMode = vk::SamplerMipmapMode::eNearest;
			}

			switch (gltfSampler.minFilter.value()) {
				case fastgltf::Filter::LinearMipMapNearest:
				case fastgltf::Filter::LinearMipMapLinear:
				case fastgltf::Filter::NearestMipMapLinear:
				case fastgltf::Filter::NearestMipMapNearest:
					samplerCI.MaxLod = 16.0f;
					break;

				default:
					samplerCI.AnisotropyEnable = VK_FALSE;
					samplerCI.MaxLod           = 0.0f;
					break;
			}
		}
		switch (gltfSampler.wrapS) {
			case fastgltf::Wrap::ClampToEdge:
				samplerCI.AddressModeU = vk::SamplerAddressMode::eClampToEdge;
				break;
			case fastgltf::Wrap::MirroredRepeat:
				samplerCI.AddressModeU = vk::SamplerAddressMode::eMirroredRepeat;
				break;
			case fastgltf::Wrap::Repeat:
			default:
				samplerCI.AddressModeU = vk::SamplerAddressMode::eRepeat;
				break;
		}
		switch (gltfSampler.wrapT) {
			case fastgltf::Wrap::ClampToEdge:
				samplerCI.AddressModeV = vk::SamplerAddressMode::eClampToEdge;
				break;
			case fastgltf::Wrap::MirroredRepeat:
				samplerCI.AddressModeV = vk::SamplerAddressMode::eMirroredRepeat;
				break;
			case fastgltf::Wrap::Repeat:
			default:
				samplerCI.AddressModeV = vk::SamplerAddressMode::eRepeat;
				break;
		}

		sampler.Handle = device.RequestSampler(samplerCI);
	}

	const Luna::Vulkan::SamplerCreateInfo samplerCI{
		.MagFilter        = vk::Filter::eLinear,
		.MinFilter        = vk::Filter::eLinear,
		.MipmapMode       = vk::SamplerMipmapMode::eLinear,
		.AddressModeU     = vk::SamplerAddressMode::eRepeat,
		.AddressModeV     = vk::SamplerAddressMode::eRepeat,
		.AnisotropyEnable = device.GetDeviceInfo().EnabledFeatures.Core.samplerAnisotropy,
		.MaxAnisotropy    = device.GetDeviceInfo().Properties.Core.limits.maxSamplerAnisotropy,
		.MinLod           = 0.0f,
		.MaxLod           = 16.0f};
	context.DefaultSampler->Handle = device.RequestSampler(samplerCI);
}

static void ImportTextures(GltfContext& context) {
	if (!context.Asset) { return; }

	const auto& asset         = *context.Asset;
	const size_t textureCount = asset.textures.size();
	for (size_t i = 0; i < textureCount; ++i) {
		const auto& gltfTexture = asset.textures[i];
		auto& texture           = context.Textures[i];

		texture.Image = &context.Images[gltfTexture.imageIndex.value()];
		if (gltfTexture.samplerIndex) {
			texture.Sampler = &context.Samplers[gltfTexture.samplerIndex.value()];
		} else {
			texture.Sampler = context.DefaultSampler;
		}
	}
}

static void LoadBuffers(Luna::TaskComposer& composer, GltfContext& context) {
	if (!context.Asset) { return; }
	const size_t bufferCount = context.Asset->buffers.size();

	auto& bufferLoad = composer.BeginPipelineStage();
	for (size_t i = 0; i < bufferCount; ++i) {
		bufferLoad.Enqueue([&context, i]() {
			const auto& gltfBuffer = context.Asset->buffers[i];
			auto& buffer           = context.Buffers[i];
			buffer.Index           = i;

			std::visit(
				Overloaded{
					[](auto& arg) {
						throw std::runtime_error("[SceneLoader] Could not load glTF buffer data: Unknown data source.");
					},
					[&](const fastgltf::sources::Vector& vector) { buffer.Data = vector.bytes; },
					[&](const fastgltf::sources::FilePath& filePath) {
						auto* filesystem = Luna::Filesystem::Get();
						const auto path  = GetPath(context, filePath.path);
						auto map         = filesystem->OpenReadOnlyMapping(path);
						if (!map) {
							throw std::runtime_error("[SceneLoader] Could not load glTF buffer data: Failed to read external file.");
						}
						const uint8_t* dataStart = map->Data<uint8_t>() + filePath.fileByteOffset;
						buffer.Data              = {dataStart, dataStart + gltfBuffer.byteLength};
					},
				},
				gltfBuffer.data);
		});
	}
}

void SceneLoader::LoadGltf(Luna::Vulkan::Device& device, Luna::Scene& scene, const Luna::Path& gltfPath) {
	auto* filesystem = Luna::Filesystem::Get();

	GltfContext context = {.Device       = device,
	                       .Scene        = scene,
	                       .GltfPath     = gltfPath,
	                       .GltfFolder   = gltfPath.BaseDirectory(),
	                       .GltfFolderFs = filesystem->GetFilesystemPath(gltfPath.BaseDirectory())};
	Luna::TaskComposer composer;

	Parse(context);
	if (!context.Asset) { return; }

	Preallocate(context);

	auto& importing = composer.BeginPipelineStage();
	importing.Enqueue([&context]() { ImportSamplers(context); });
	importing.Enqueue([&context]() { ImportTextures(context); });

	LoadBuffers(composer, context);

	auto final = composer.GetOutgoingTask();
	final->Wait();

	if (!context.Asset) { Luna::Log::Error("SceneLoader", "Failed to load glTF file to scene!"); }
}
