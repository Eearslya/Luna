#include "SceneLoader.hpp"

#include <Luna/Platform/Filesystem.hpp>
#include <Luna/Scene/Entity.hpp>
#include <Luna/Scene/MeshRendererComponent.hpp>
#include <Luna/Scene/Scene.hpp>
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
#include <glm/gtc/type_ptr.hpp>

template <typename T>
struct AccessorType {
	using UnderlyingT                                  = void;
	constexpr static fastgltf::AccessorType Type       = fastgltf::AccessorType::Invalid;
	constexpr static fastgltf::ComponentType Component = fastgltf::ComponentType::Invalid;
	constexpr static size_t Count                      = 0;
};
template <>
struct AccessorType<glm::vec2> {
	using UnderlyingT                                  = typename glm::vec2::value_type;
	constexpr static fastgltf::AccessorType Type       = fastgltf::AccessorType::Vec2;
	constexpr static fastgltf::ComponentType Component = fastgltf::ComponentType::Float;
	constexpr static size_t Count                      = 2;
};
template <>
struct AccessorType<glm::vec3> {
	using UnderlyingT                                  = typename glm::vec3::value_type;
	constexpr static fastgltf::AccessorType Type       = fastgltf::AccessorType::Vec3;
	constexpr static fastgltf::ComponentType Component = fastgltf::ComponentType::Float;
	constexpr static size_t Count                      = 3;
};
template <>
struct AccessorType<glm::vec4> {
	using UnderlyingT                                  = typename glm::vec4::value_type;
	constexpr static fastgltf::AccessorType Type       = fastgltf::AccessorType::Vec4;
	constexpr static fastgltf::ComponentType Component = fastgltf::ComponentType::Float;
	constexpr static size_t Count                      = 4;
};
template <>
struct AccessorType<glm::uvec4> {
	using UnderlyingT                                  = typename glm::uvec4::value_type;
	constexpr static fastgltf::AccessorType Type       = fastgltf::AccessorType::Vec4;
	constexpr static fastgltf::ComponentType Component = fastgltf::ComponentType::UnsignedInt;
	constexpr static size_t Count                      = 4;
};
template <>
struct AccessorType<uint8_t> {
	using UnderlyingT                                  = uint8_t;
	constexpr static fastgltf::AccessorType Type       = fastgltf::AccessorType::Scalar;
	constexpr static fastgltf::ComponentType Component = fastgltf::ComponentType::UnsignedByte;
	constexpr static size_t Count                      = 1;
};
template <>
struct AccessorType<uint16_t> {
	using UnderlyingT                                  = uint16_t;
	constexpr static fastgltf::AccessorType Type       = fastgltf::AccessorType::Scalar;
	constexpr static fastgltf::ComponentType Component = fastgltf::ComponentType::UnsignedShort;
	constexpr static size_t Count                      = 1;
};
template <>
struct AccessorType<uint32_t> {
	using UnderlyingT                                  = uint32_t;
	constexpr static fastgltf::AccessorType Type       = fastgltf::AccessorType::Scalar;
	constexpr static fastgltf::ComponentType Component = fastgltf::ComponentType::UnsignedInt;
	constexpr static size_t Count                      = 1;
};

enum class VertexAttributeBits {
	Position  = 1 << 1,
	Normal    = 1 << 2,
	Tangent   = 1 << 3,
	Texcoord0 = 1 << 4,
	Texcoord1 = 1 << 5,
	Color0    = 1 << 6,
	Joints0   = 1 << 7,
	Weights0  = 1 << 8,
	Index     = 1 << 9
};
using VertexAttributes = Luna::Bitmask<VertexAttributeBits>;
template <>
struct Luna::EnableBitmaskOperators<VertexAttributeBits> : std::true_type {};

enum class MeshProcessingStepBits {
	UnpackVertices       = 1 << 1,
	GenerateFlatNormals  = 1 << 2,
	GenerateTangentSpace = 1 << 3,
	WeldVertices         = 1 << 4
};
using MeshProcessingSteps = Luna::Bitmask<MeshProcessingStepBits>;
template <>
struct Luna::EnableBitmaskOperators<MeshProcessingStepBits> : std::true_type {};

template <class... Ts>
struct Overloaded : Ts... {
	using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

struct Vertex {
	glm::vec3 Normal    = glm::vec3(0.0f);
	glm::vec4 Tangent   = glm::vec4(0.0f);
	glm::vec2 Texcoord0 = glm::vec2(0.0f);
	glm::vec2 Texcoord1 = glm::vec2(0.0f);
	glm::vec4 Color0    = glm::vec4(0.0f);
	glm::uvec4 Joints0  = glm::uvec4(0);
	glm::vec4 Weights0  = glm::vec4(0.0f);

	bool operator==(const Vertex& other) const {
		return Normal == other.Normal && Tangent == other.Tangent && Texcoord0 == other.Texcoord0 &&
		       Texcoord1 == other.Texcoord1 && Color0 == other.Color0 && Joints0 == other.Joints0 &&
		       Weights0 == other.Weights0;
	}
};

template <>
struct std::hash<Vertex> {
	size_t operator()(const Vertex& v) {
		Luna::Hasher h;
		h.Data(sizeof(v.Normal), glm::value_ptr(v.Normal));
		h.Data(sizeof(v.Tangent), glm::value_ptr(v.Tangent));
		h.Data(sizeof(v.Texcoord0), glm::value_ptr(v.Texcoord0));
		h.Data(sizeof(v.Texcoord1), glm::value_ptr(v.Texcoord1));
		h.Data(sizeof(v.Color0), glm::value_ptr(v.Color0));
		h.Data(sizeof(v.Joints0), glm::value_ptr(v.Joints0));
		h.Data(sizeof(v.Weights0), glm::value_ptr(v.Weights0));

		return static_cast<size_t>(h.Get());
	}
};

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

struct Submesh {
	Material* Material         = nullptr;
	vk::DeviceSize VertexCount = 0;
	vk::DeviceSize IndexCount  = 0;
	vk::DeviceSize FirstVertex = 0;
	vk::DeviceSize FirstIndex  = 0;
};

struct Node {
	uint32_t Index = 0;
	Node* Parent   = nullptr;
	std::vector<Node*> Children;
	int32_t MeshIndex = -1;

	glm::vec3 Translation = glm::vec3(0.0f);
	glm::vec3 Rotation    = glm::vec3(0.0f);
	glm::vec3 Scale       = glm::vec3(1.0f);
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
	std::vector<Luna::IntrusivePtr<Luna::StaticMesh>> Meshes;
	std::vector<Node> Nodes;
	std::vector<Sampler> Samplers;
	std::vector<Texture> Textures;

	Material* DefaultMaterial = nullptr;
	Sampler* DefaultSampler   = nullptr;
	std::vector<Node*> RootNodes;
};

template <typename Source, typename Destination>
static std::vector<Destination> ConvertAccessorData(const GltfContext& context,
                                                    const fastgltf::Accessor& gltfAccessor,
                                                    bool vertexAccessor) {
	static_assert(AccessorType<Destination>::Count > 0, "Unknown type conversion given to ConvertAccessorData");
	const auto& gltfModel = *context.Asset;

	using D = typename AccessorType<Destination>::UnderlyingT;

	constexpr auto dstCount   = AccessorType<Destination>::Count;
	constexpr Source srcMax   = std::numeric_limits<Source>::max();
	constexpr bool srcSigned  = std::numeric_limits<Source>::is_signed;
	constexpr auto srcSize    = sizeof(Source);
	constexpr auto attrStride = srcSize * dstCount;
	// Accessors used for vertex data must have each element aligned to 4-byte boundaries.
	constexpr auto vertexStride = attrStride % 4 == 0 ? attrStride : attrStride + 4 - (attrStride % 4);

	const auto count           = gltfAccessor.count;
	const auto normalized      = gltfAccessor.normalized;
	const auto& gltfBufferView = gltfModel.bufferViews[*gltfAccessor.bufferViewIndex];
	const auto& gltfBytes      = context.Buffers[gltfBufferView.bufferIndex].Data;
	const uint8_t* bufferData  = &gltfBytes.data()[gltfAccessor.byteOffset + gltfBufferView.byteOffset];
	const auto byteStride      = gltfBufferView.byteStride.value_or(vertexAccessor ? vertexStride : attrStride);

	auto Get = [bufferData, byteStride, normalized](size_t attributeIndex, uint8_t componentIndex) -> D {
		const uint8_t* dataPtr = bufferData + (attributeIndex * byteStride) + (componentIndex * srcSize);
		const Source v         = *reinterpret_cast<const Source*>(dataPtr);

		if (normalized) {
			if (srcSigned) {
				return std::max(static_cast<D>(v) / static_cast<D>(srcMax), static_cast<D>(-1.0));
			} else {
				return static_cast<D>(v) / static_cast<D>(srcMax);
			}
		} else {
			return static_cast<D>(v);
		}
	};

	std::vector<Destination> dst(count);

	if constexpr (dstCount == 1) {
		for (size_t i = 0; i < count; ++i) { dst[i] = static_cast<D>(Get(i, 0)); }
	} else {
		for (size_t i = 0; i < count; ++i) {
			dst[i][0] = static_cast<D>(Get(i, 0));
			dst[i][1] = static_cast<D>(Get(i, 1));
			if constexpr (dstCount >= 3) { dst[i][2] = static_cast<D>(Get(i, 2)); }
			if constexpr (dstCount >= 4) { dst[i][3] = static_cast<D>(Get(i, 3)); }
		}
	}

	return dst;
}

template <typename T>
static std::vector<T> GetAccessorData(const GltfContext& context,
                                      const fastgltf::Accessor& gltfAccessor,
                                      bool vertexAccessor = false) {
	constexpr auto outType          = AccessorType<T>::Type;
	constexpr auto outComponentType = AccessorType<T>::Component;
	const auto accessorType         = gltfAccessor.type;

	if (outType == accessorType) {
		switch (gltfAccessor.componentType) {
			case fastgltf::ComponentType::Byte:
				return ConvertAccessorData<int8_t, T>(context, gltfAccessor, vertexAccessor);
			case fastgltf::ComponentType::UnsignedByte:
				return ConvertAccessorData<uint8_t, T>(context, gltfAccessor, vertexAccessor);
			case fastgltf::ComponentType::Short:
				return ConvertAccessorData<int16_t, T>(context, gltfAccessor, vertexAccessor);
			case fastgltf::ComponentType::UnsignedShort:
				return ConvertAccessorData<uint16_t, T>(context, gltfAccessor, vertexAccessor);
			case fastgltf::ComponentType::UnsignedInt:
				return ConvertAccessorData<uint32_t, T>(context, gltfAccessor, vertexAccessor);
			case fastgltf::ComponentType::Float:
				return ConvertAccessorData<float, T>(context, gltfAccessor, vertexAccessor);
			case fastgltf::ComponentType::Double:
				return ConvertAccessorData<double, T>(context, gltfAccessor, vertexAccessor);
			default:
				break;
		}
	}

	return {};
}

template <typename T>
static std::vector<T> GetAccessorData(const GltfContext& context,
                                      const fastgltf::Primitive& gltfPrimitive,
                                      VertexAttributeBits attribute) {
	const auto& gltfModel = *context.Asset;

	if (attribute == VertexAttributeBits::Index) {
		if (gltfPrimitive.indicesAccessor.has_value()) {
			return GetAccessorData<T>(context, gltfModel.accessors[*gltfPrimitive.indicesAccessor]);
		}
	} else {
		auto it = gltfPrimitive.attributes.end();
		switch (attribute) {
			case VertexAttributeBits::Position:
				it = gltfPrimitive.attributes.find("POSITION");
				break;
			case VertexAttributeBits::Normal:
				it = gltfPrimitive.attributes.find("NORMAL");
				break;
			case VertexAttributeBits::Tangent:
				it = gltfPrimitive.attributes.find("TANGENT");
				break;
			case VertexAttributeBits::Texcoord0:
				it = gltfPrimitive.attributes.find("TEXCOORD_0");
				break;
			case VertexAttributeBits::Texcoord1:
				it = gltfPrimitive.attributes.find("TEXCOORD_1");
				break;
			case VertexAttributeBits::Color0:
				it = gltfPrimitive.attributes.find("COLOR_0");
				break;
			case VertexAttributeBits::Joints0:
				it = gltfPrimitive.attributes.find("JOINTS_0");
				break;
			case VertexAttributeBits::Weights0:
				it = gltfPrimitive.attributes.find("WEIGHTS_0");
				break;
			default:
				throw std::runtime_error("Requested unknown vertex attribute!");
				break;
		}

		if (it != gltfPrimitive.attributes.end()) {
			return GetAccessorData<T>(context, gltfModel.accessors[it->second], true);
		}
	}

	return {};
}

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
	context.Materials.resize(context.Asset->materials.size() + 1);
	context.Meshes.resize(context.Asset->meshes.size());
	context.Nodes.resize(context.Asset->nodes.size());
	context.Samplers.resize(context.Asset->samplers.size() + 1);
	context.Textures.resize(context.Asset->textures.size());

	context.DefaultMaterial = &context.Materials.back();
	context.DefaultSampler  = &context.Samplers.back();
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

static void ImportMaterials(GltfContext& context) {
	if (!context.Asset) { return; }
}

static void ImportNodes(GltfContext& context) {
	if (!context.Asset) { return; }

	const auto& gltfScene = context.Asset->scenes[context.Asset->defaultScene.value_or(0)];

	for (const auto nodeIndex : gltfScene.nodeIndices) { context.RootNodes.push_back(&context.Nodes[nodeIndex]); }

	for (size_t i = 0; i < gltfScene.nodeIndices.size(); ++i) {
		const auto& gltfNode = context.Asset->nodes[i];
		auto& node           = context.Nodes[i];

		node.Index     = i;
		node.MeshIndex = gltfNode.meshIndex ? int(*gltfNode.meshIndex) : -1;

		std::visit(Overloaded{[&](const fastgltf::Node::TRS& trs) {
														node.Translation = glm::make_vec3(trs.translation.data());
														node.Scale       = glm::make_vec3(trs.scale.data());
														glm::quat rot;
														rot.x         = trs.rotation[0];
														rot.y         = trs.rotation[1];
														rot.z         = trs.rotation[2];
														rot.w         = trs.rotation[3];
														node.Rotation = glm::degrees(glm::eulerAngles(rot));
													},
		                      [&](const fastgltf::Node::TransformMatrix& mat) {}},
		           gltfNode.transform);

		for (const auto childIndex : gltfNode.children) {
			node.Children.push_back(&context.Nodes[childIndex]);
			context.Nodes[childIndex].Parent = &context.Nodes[i];
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

static void LoadMeshes(Luna::TaskComposer& composer, GltfContext& context) {
	if (!context.Asset) { return; }
	const size_t meshCount = context.Asset->meshes.size();

	auto& meshLoad = composer.BeginPipelineStage();
	for (size_t i = 0; i < meshCount; ++i) {
		meshLoad.Enqueue([&context, i]() {
			const auto& gltfMesh = context.Asset->meshes[i];
			auto& mesh           = context.Meshes[i];
			mesh                 = Luna::MakeHandle<Luna::StaticMesh>();

			const size_t defaultMaterialIndex               = context.Materials.size() - 1;
			std::vector<fastgltf::Primitive> gltfPrimitives = gltfMesh.primitives;
			std::sort(gltfPrimitives.begin(),
			          gltfPrimitives.end(),
			          [defaultMaterialIndex](const fastgltf::Primitive& a, const fastgltf::Primitive& b) -> bool {
									return a.materialIndex.value_or(defaultMaterialIndex) >
				                 b.materialIndex.value_or(defaultMaterialIndex);
								});

			std::vector<std::vector<int>> materialPrimitives(context.Materials.size());
			for (uint32_t i = 0; i < gltfPrimitives.size(); ++i) {
				const auto& gltfPrimitive = gltfPrimitives[i];
				materialPrimitives[gltfPrimitive.materialIndex.value_or(defaultMaterialIndex)].push_back(i);
			}
			materialPrimitives.erase(std::remove_if(materialPrimitives.begin(),
			                                        materialPrimitives.end(),
			                                        [](const std::vector<int>& material) { return material.empty(); }),
			                         materialPrimitives.end());

			std::vector<Submesh> submeshes;
			std::vector<glm::vec3> meshPositions;
			std::vector<Vertex> meshVertices;
			std::vector<uint32_t> meshIndices;

			for (size_t materialIndex = 0; materialIndex < materialPrimitives.size(); ++materialIndex) {
				const auto& primitiveList = materialPrimitives[materialIndex];
				auto& submesh             = submeshes.emplace_back();

				const size_t gltfMaterialIndex = gltfPrimitives[primitiveList[0]].materialIndex.value_or(defaultMaterialIndex);
				submesh.Material               = &context.Materials[gltfMaterialIndex];

				submesh.FirstVertex = meshVertices.size();
				submesh.FirstIndex  = meshIndices.size();
				submesh.VertexCount = 0;
				submesh.IndexCount  = 0;

				glm::vec3 boundsMin(std::numeric_limits<float>::max());
				glm::vec3 boundsMax(std::numeric_limits<float>::lowest());

				for (const auto gltfPrimitiveIndex : primitiveList) {
					const auto& gltfPrimitive = gltfPrimitives[gltfPrimitiveIndex];

					std::vector<glm::vec3> positions;
					std::vector<Vertex> vertices;
					std::vector<uint32_t> indices;

					{
						positions    = GetAccessorData<glm::vec3>(context, gltfPrimitive, VertexAttributeBits::Position);
						auto normals = GetAccessorData<glm::vec3>(context, gltfPrimitive, VertexAttributeBits::Normal);

						normals.resize(positions.size());

						vertices.reserve(positions.size());
						for (size_t i = 0; i < positions.size(); ++i) { vertices.push_back(Vertex{.Normal = normals[i]}); }

						indices = GetAccessorData<uint32_t>(context, gltfPrimitive, VertexAttributeBits::Index);
					}

					for (const auto& v : positions) {
						boundsMin = glm::min(v, boundsMin);
						boundsMax = glm::max(v, boundsMax);
					}

					for (auto& i : indices) { i += submesh.VertexCount; }

					meshPositions.reserve(meshPositions.size() + positions.size());
					meshPositions.insert(meshPositions.end(), positions.begin(), positions.end());
					meshVertices.reserve(meshVertices.size() + vertices.size());
					meshVertices.insert(meshVertices.end(), vertices.begin(), vertices.end());
					meshIndices.reserve(meshIndices.size() + indices.size());
					meshIndices.insert(meshIndices.end(), indices.begin(), indices.end());

					submesh.VertexCount += positions.size();
					submesh.IndexCount += indices.size();
				}
			}

			const vk::DeviceSize positionSize = meshPositions.size() * sizeof(glm::vec3);
			const vk::DeviceSize vertexSize   = meshVertices.size() * sizeof(Vertex);
			const vk::DeviceSize indexSize    = meshIndices.size() * sizeof(uint32_t);

			std::vector<uint8_t> posBufferData(positionSize + indexSize);
			memcpy(posBufferData.data(), meshPositions.data(), positionSize);
			memcpy(posBufferData.data() + positionSize, meshIndices.data(), indexSize);

			std::vector<uint8_t> attrBufferData(vertexSize);
			memcpy(attrBufferData.data(), meshVertices.data(), vertexSize);

			const Luna::Vulkan::BufferCreateInfo posBufferCI(
				Luna::Vulkan::BufferDomain::Device,
				positionSize + indexSize,
				vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer);
			mesh->PositionBuffer = context.Device.CreateBuffer(posBufferCI, posBufferData.data());

			mesh->PositionStride                                            = sizeof(glm::vec3);
			mesh->IndexOffset                                               = positionSize;
			mesh->IndexType                                                 = vk::IndexType::eUint32;
			mesh->Attributes[int(Luna::MeshAttributeType::Position)].Format = vk::Format::eR32G32B32Sfloat;
			mesh->Attributes[int(Luna::MeshAttributeType::Position)].Offset = 0;

			const Luna::Vulkan::BufferCreateInfo attrBufferCI(
				Luna::Vulkan::BufferDomain::Device, vertexSize, vk::BufferUsageFlagBits::eVertexBuffer);
			mesh->AttributeBuffer = context.Device.CreateBuffer(attrBufferCI, attrBufferData.data());
			mesh->AttributeStride = sizeof(Vertex);
			mesh->Attributes[int(Luna::MeshAttributeType::Normal)].Format = vk::Format::eR32G32B32Sfloat;
			mesh->Attributes[int(Luna::MeshAttributeType::Normal)].Offset = offsetof(Vertex, Normal);

			for (const auto& submesh : submeshes) {
				mesh->AddSubmesh(0, submesh.VertexCount, submesh.IndexCount, submesh.FirstVertex, submesh.FirstIndex);
			}
		});
	}
}

static void PopulateScene(GltfContext& context) {
	const std::function<void(Node*, Luna::Entity)> AddNode = [&](Node* node, Luna::Entity parent) {
		auto entity = context.Scene.CreateChildEntity(parent);

		entity.Translate(node->Translation);
		entity.Rotate(node->Rotation);
		entity.Scale(node->Scale);

		if (node->MeshIndex != -1) {
			auto& cMeshRenderer      = entity.AddComponent<Luna::MeshRendererComponent>();
			cMeshRenderer.StaticMesh = context.Meshes[node->MeshIndex];
		}

		for (auto* childNode : node->Children) { AddNode(childNode, entity); }
	};

	for (auto* node : context.RootNodes) { AddNode(node, Luna::Entity{}); }
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
	importing.Enqueue([&context]() { ImportMaterials(context); });
	importing.Enqueue([&context]() { ImportNodes(context); });

	LoadBuffers(composer, context);
	LoadMeshes(composer, context);

	auto& addToScene = composer.BeginPipelineStage();
	addToScene.Enqueue([&context]() { PopulateScene(context); });

	auto final = composer.GetOutgoingTask();
	final->Wait();

	if (!context.Asset) { Luna::Log::Error("SceneLoader", "Failed to load glTF file to scene!"); }
}
