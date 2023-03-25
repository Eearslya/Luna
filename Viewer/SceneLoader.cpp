#include "SceneLoader.hpp"

#include <mikktspace.h>
#include <stb_image.h>

#include <Luna/Platform/Filesystem.hpp>
#include <Luna/Renderer/Material.hpp>
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
#include <glm/gtx/normal.hpp>

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

struct CombinedVertex {
	glm::vec3 Position;
	Vertex Attributes;

	bool operator==(const CombinedVertex& other) const {
		return Position == other.Position && Attributes == other.Attributes;
	}
};

template <>
struct std::hash<CombinedVertex> {
	size_t operator()(const CombinedVertex& v) const {
		Luna::Hasher h;
		h.Data(sizeof(v.Position), glm::value_ptr(v.Position));
		h(v.Attributes);

		return static_cast<size_t>(h.Get());
	}
};

struct Buffer {
	uint32_t Index = 0;
	std::vector<uint8_t> Data;
};

struct Sampler {
	Luna::Vulkan::Sampler* Handle = nullptr;
};

struct Submesh {
	uint32_t MaterialIndex     = 0;
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
	std::vector<Luna::Vulkan::ImageHandle> Images;
	std::vector<Luna::IntrusivePtr<Luna::Material>> Materials;
	std::vector<Luna::IntrusivePtr<Luna::StaticMesh>> Meshes;
	std::vector<Node> Nodes;
	std::vector<Sampler> Samplers;

	Sampler* DefaultSampler = nullptr;
	std::vector<Node*> RootNodes;
};

struct MikkTContext {
	std::vector<glm::vec3>& Positions;
	std::vector<Vertex>& Vertices;
};

/* ============================
** === MikkTSpace Functions ===
** ============================ */
static int MikkTGetNumFaces(const SMikkTSpaceContext* context) {
	const auto data = reinterpret_cast<const MikkTContext*>(context->m_pUserData);
	return data->Vertices.size() / 3;
}
static int MikkTGetNumVerticesOfFace(const SMikkTSpaceContext* context, const int face) {
	return 3;
}
static void MikkTGetPosition(const SMikkTSpaceContext* context, float fvPosOut[], const int face, const int vert) {
	const auto data     = reinterpret_cast<const MikkTContext*>(context->m_pUserData);
	const glm::vec3 pos = data->Positions[face * 3 + vert];
	fvPosOut[0]         = pos.x;
	fvPosOut[1]         = pos.y;
	fvPosOut[2]         = pos.z;
}
static void MikkTGetNormal(const SMikkTSpaceContext* context, float fvNormOut[], const int face, const int vert) {
	const auto data      = reinterpret_cast<const MikkTContext*>(context->m_pUserData);
	const glm::vec3 norm = data->Vertices[face * 3 + vert].Normal;
	fvNormOut[0]         = norm.x;
	fvNormOut[1]         = norm.y;
	fvNormOut[2]         = norm.z;
}
static void MikkTGetTexCoord(const SMikkTSpaceContext* context, float fvTexcOut[], const int face, const int vert) {
	const auto data = reinterpret_cast<const MikkTContext*>(context->m_pUserData);
	glm::vec2 uv    = data->Vertices[face * 3 + vert].Texcoord0;
	fvTexcOut[0]    = uv.x;
	fvTexcOut[1]    = 1.0f - uv.y;
}
static void MikkTSetTSpaceBasic(
	const SMikkTSpaceContext* context, const float fvTangent[], const float fSign, const int face, const int vert) {
	auto data = reinterpret_cast<MikkTContext*>(context->m_pUserData);

	data->Vertices[face * 3 + vert].Tangent = glm::vec4(glm::make_vec3(fvTangent), fSign);
}
static SMikkTSpaceInterface MikkTInterface = {.m_getNumFaces          = MikkTGetNumFaces,
                                              .m_getNumVerticesOfFace = MikkTGetNumVerticesOfFace,
                                              .m_getPosition          = MikkTGetPosition,
                                              .m_getNormal            = MikkTGetNormal,
                                              .m_getTexCoord          = MikkTGetTexCoord,
                                              .m_setTSpaceBasic       = MikkTSetTSpaceBasic,
                                              .m_setTSpace            = nullptr};

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

static void LoadImages(Luna::TaskComposer& composer, GltfContext& context) {
	if (!context.Asset) { return; }
	const size_t imageCount = context.Asset->images.size();

	auto& imageLoad = composer.BeginPipelineStage();
	for (size_t i = 0; i < imageCount; ++i) {
		imageLoad.Enqueue([&context, i]() {
			const auto& gltfImage = context.Asset->images[i];
			auto& image           = context.Images[i];

			std::vector<uint8_t> bytes;
			std::visit(Overloaded{[&](auto& arg) {},
			                      [&](const fastgltf::sources::FilePath& filePath) {
															auto* filesystem = Luna::Filesystem::Get();
															const auto path  = GetPath(context, filePath.path);
															auto map         = filesystem->OpenReadOnlyMapping(path);
															if (!map) {
																throw std::runtime_error(
																	"[SceneLoader] Could not load glTF image data: Failed to read external file.");
															}
															const uint8_t* dataStart = map->Data<uint8_t>();
															bytes                    = {dataStart, dataStart + map->GetSize()};
														},
			                      [&](const fastgltf::sources::Vector& vector) { bytes = vector.bytes; }},
			           gltfImage.data);
			if (bytes.empty()) { throw std::runtime_error("[SceneLoader] Could not load glTF image!"); }

			int width, height, components;
			stbi_set_flip_vertically_on_load(0);
			stbi_uc* pixels = stbi_load_from_memory(bytes.data(), bytes.size(), &width, &height, &components, 4);
			if (pixels == nullptr) { throw std::runtime_error("[SceneLoader] Failed to load glTF image!"); }

			const Luna::Vulkan::ImageInitialData initialData = {.Data = pixels};
			Luna::Vulkan::ImageCreateInfo imageCI =
				Luna::Vulkan::ImageCreateInfo::Immutable2D(vk::Format::eR8G8B8A8Srgb, width, height, true);
			imageCI.MiscFlags |= Luna::Vulkan::ImageCreateFlagBits::MutableSrgb;
			image = context.Device.CreateImage(imageCI, &initialData);

			stbi_image_free(pixels);
		});
	}
}

static void LoadMaterials(Luna::TaskComposer& composer, GltfContext& context) {
	if (!context.Asset) { return; }
	const size_t materialCount = context.Asset->materials.size();

	auto& materialLoad = composer.BeginPipelineStage();
	for (size_t i = 0; i < materialCount; ++i) {
		materialLoad.Enqueue([&context, i]() {
			const auto& gltfMaterial = context.Asset->materials[i];
			auto& material           = context.Materials[i];
			material                 = Luna::MakeHandle<Luna::Material>();

			material->DualSided = gltfMaterial.doubleSided;

			const auto Assign = [&](const std::optional<fastgltf::TextureInfo>& texInfo, Luna::Texture& texture) {
				if (!texInfo) { return; }
				const auto& gltfTexture = context.Asset->textures[texInfo->textureIndex];
				texture.Image           = context.Images[*gltfTexture.imageIndex];
				texture.Sampler         = context.Samplers[*gltfTexture.samplerIndex].Handle;
			};

			if (gltfMaterial.pbrData) {
				const auto& pbr = *gltfMaterial.pbrData;
				Assign(pbr.baseColorTexture, material->Albedo);
				Assign(pbr.metallicRoughnessTexture, material->PBR);
			}
			Assign(gltfMaterial.normalTexture, material->Normal);
			Assign(gltfMaterial.occlusionTexture, material->Occlusion);
			Assign(gltfMaterial.emissiveTexture, material->Emissive);
		});
	}
}

static VertexAttributes GetAvailableAttributes(const fastgltf::Primitive& prim) {
	VertexAttributes attr = {};

	for (const auto& [attributeName, attributeValue] : prim.attributes) {
		if (attributeName.compare("POSITION") == 0) { attr |= VertexAttributeBits::Position; }
		if (attributeName.compare("NORMAL") == 0) { attr |= VertexAttributeBits::Normal; }
		if (attributeName.compare("TANGENT") == 0) { attr |= VertexAttributeBits::Tangent; }
		if (attributeName.compare("TEXCOORD_0") == 0) { attr |= VertexAttributeBits::Texcoord0; }
		if (attributeName.compare("TEXCOORD_1") == 0) { attr |= VertexAttributeBits::Texcoord1; }
		if (attributeName.compare("COLOR_0") == 0) { attr |= VertexAttributeBits::Color0; }
		if (attributeName.compare("JOINTS_0") == 0) { attr |= VertexAttributeBits::Joints0; }
		if (attributeName.compare("WEIGHTS_0") == 0) { attr |= VertexAttributeBits::Weights0; }
	}
	if (prim.indicesAccessor.has_value()) { attr |= VertexAttributeBits::Index; }

	return attr;
}

static MeshProcessingSteps GetProcessingSteps(VertexAttributes attributes) {
	MeshProcessingSteps steps = {};

	if (!(attributes & VertexAttributeBits::Normal)) {
		// No normals provided. We must generate flat normals, then generate tangents using MikkTSpace.
		steps |= MeshProcessingStepBits::UnpackVertices;
		steps |= MeshProcessingStepBits::GenerateFlatNormals;
		steps |= MeshProcessingStepBits::GenerateTangentSpace;
		steps |= MeshProcessingStepBits::WeldVertices;
	}
	if (!(attributes & VertexAttributeBits::Tangent)) {
		// No tangents provided. We must generate tangents using MikkTSPace.
		steps |= MeshProcessingStepBits::UnpackVertices;
		steps |= MeshProcessingStepBits::GenerateTangentSpace;
		steps |= MeshProcessingStepBits::WeldVertices;
	}
	if (!(attributes & VertexAttributeBits::Index)) {
		// No indices provided. We will weld the mesh and create our own index buffer.
		steps |= MeshProcessingStepBits::WeldVertices;
	}

	return steps;
}

static void LoadMeshes(Luna::TaskComposer& composer, GltfContext& context) {
	if (!context.Asset) { return; }
	const size_t meshCount = context.Asset->meshes.size();

	auto& meshLoad = composer.BeginPipelineStage();
	for (size_t i = 0; i < meshCount; ++i) {
		meshLoad.Enqueue([&context, i]() {
			// Create a MikkTSpace context for tangent generation.
			SMikkTSpaceContext mikktContext = {.m_pInterface = &MikkTInterface};

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

			std::vector<Luna::IntrusivePtr<Luna::Material>> materials;

			std::vector<Submesh> submeshes;
			std::vector<glm::vec3> meshPositions;
			std::vector<Vertex> meshVertices;
			std::vector<uint32_t> meshIndices;

			for (size_t materialIndex = 0; materialIndex < materialPrimitives.size(); ++materialIndex) {
				const auto& primitiveList = materialPrimitives[materialIndex];
				auto& submesh             = submeshes.emplace_back();

				const size_t gltfMaterialIndex = gltfPrimitives[primitiveList[0]].materialIndex.value_or(defaultMaterialIndex);
				submesh.MaterialIndex          = materials.size();
				materials.push_back(context.Materials[gltfMaterialIndex]);

				submesh.FirstVertex = meshVertices.size();
				submesh.FirstIndex  = meshIndices.size();
				submesh.VertexCount = 0;
				submesh.IndexCount  = 0;

				glm::vec3 boundsMin(std::numeric_limits<float>::max());
				glm::vec3 boundsMax(std::numeric_limits<float>::lowest());

				for (const auto gltfPrimitiveIndex : primitiveList) {
					const auto& gltfPrimitive = gltfPrimitives[gltfPrimitiveIndex];
					const auto primAttributes = GetAvailableAttributes(gltfPrimitive);
					const auto primProcessing = GetProcessingSteps(primAttributes);

					std::vector<glm::vec3> positions;
					std::vector<Vertex> vertices;
					std::vector<uint32_t> indices;

					{
						positions      = GetAccessorData<glm::vec3>(context, gltfPrimitive, VertexAttributeBits::Position);
						auto normals   = GetAccessorData<glm::vec3>(context, gltfPrimitive, VertexAttributeBits::Normal);
						auto tangents  = GetAccessorData<glm::vec4>(context, gltfPrimitive, VertexAttributeBits::Tangent);
						auto texcoord0 = GetAccessorData<glm::vec2>(context, gltfPrimitive, VertexAttributeBits::Texcoord0);

						normals.resize(positions.size());
						tangents.resize(positions.size());
						texcoord0.resize(positions.size());

						vertices.reserve(positions.size());
						for (size_t i = 0; i < positions.size(); ++i) {
							vertices.push_back(Vertex{.Normal = normals[i], .Tangent = tangents[i], .Texcoord0 = texcoord0[i]});
						}

						indices = GetAccessorData<uint32_t>(context, gltfPrimitive, VertexAttributeBits::Index);
					}

					if (primProcessing & MeshProcessingStepBits::UnpackVertices) {
						if (indices.size() > 0) {
							std::vector<glm::vec3> newPositions(indices.size());
							std::vector<Vertex> newAttributes(indices.size());

							uint32_t newIndex = 0;
							for (const uint32_t index : indices) {
								newPositions[newIndex]  = positions[index];
								newAttributes[newIndex] = vertices[index];
								++newIndex;
							}
							positions = std::move(newPositions);
							vertices  = std::move(newAttributes);
						}
					}

					if (primProcessing & MeshProcessingStepBits::GenerateFlatNormals) {
						const size_t faceCount = vertices.size() / 3;

						for (size_t i = 0; i < faceCount; ++i) {
							auto& p1     = positions[i * 3 + 0];
							auto& p2     = positions[i * 3 + 1];
							auto& p3     = positions[i * 3 + 2];
							auto& v1     = vertices[i * 3 + 0];
							auto& v2     = vertices[i * 3 + 1];
							auto& v3     = vertices[i * 3 + 2];
							const auto n = glm::normalize(glm::triangleNormal(p1, p2, p3));

							v1.Normal = n;
							v2.Normal = n;
							v3.Normal = n;
						}
					}

					if (primProcessing & MeshProcessingStepBits::GenerateTangentSpace) {
						MikkTContext context{positions, vertices};
						mikktContext.m_pUserData = &context;
						genTangSpaceDefault(&mikktContext);
					}

					if (primProcessing & MeshProcessingStepBits::WeldVertices) {
						indices.clear();
						indices.reserve(vertices.size());
						std::unordered_map<CombinedVertex, uint32_t> uniqueVertices;

						const size_t oldVertexCount = vertices.size();
						uint32_t newVertexCount     = 0;
						for (size_t i = 0; i < oldVertexCount; ++i) {
							const CombinedVertex v = {positions[i], vertices[i]};

							const auto it = uniqueVertices.find(v);
							if (it == uniqueVertices.end()) {
								const uint32_t index = newVertexCount++;
								uniqueVertices.insert(std::make_pair(v, index));
								positions[index] = v.Position;
								vertices[index]  = v.Attributes;
								indices.push_back(index);
							} else {
								indices.push_back(it->second);
							}
						}
						positions.resize(newVertexCount);
						vertices.resize(newVertexCount);
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
			mesh->Attributes[int(Luna::MeshAttributeType::Normal)].Format    = vk::Format::eR32G32B32Sfloat;
			mesh->Attributes[int(Luna::MeshAttributeType::Normal)].Offset    = offsetof(Vertex, Normal);
			mesh->Attributes[int(Luna::MeshAttributeType::Tangent)].Format   = vk::Format::eR32G32B32A32Sfloat;
			mesh->Attributes[int(Luna::MeshAttributeType::Tangent)].Offset   = offsetof(Vertex, Tangent);
			mesh->Attributes[int(Luna::MeshAttributeType::Texcoord0)].Format = vk::Format::eR32G32Sfloat;
			mesh->Attributes[int(Luna::MeshAttributeType::Texcoord0)].Offset = offsetof(Vertex, Texcoord0);

			for (const auto& submesh : submeshes) {
				mesh->AddSubmesh(
					submesh.MaterialIndex, submesh.VertexCount, submesh.IndexCount, submesh.FirstVertex, submesh.FirstIndex);
			}
			mesh->Materials = std::move(materials);
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
	importing.Enqueue([&context]() { ImportNodes(context); });

	LoadBuffers(composer, context);
	LoadImages(composer, context);
	LoadMaterials(composer, context);
	LoadMeshes(composer, context);

	auto& addToScene = composer.BeginPipelineStage();
	addToScene.Enqueue([&context]() { PopulateScene(context); });

	auto final = composer.GetOutgoingTask();
	final->Wait();

	if (!context.Asset) { Luna::Log::Error("SceneLoader", "Failed to load glTF file to scene!"); }
}
