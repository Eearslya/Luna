#include <mikktspace.h>

#include <Luna/Assets/AssetManager.hpp>
#include <Luna/Assets/Mesh.hpp>
#include <Luna/Editor/MeshGltfImporter.hpp>
#include <Luna/Platform/Filesystem.hpp>
#include <Luna/Utility/Path.hpp>
#include <fastgltf_parser.hpp>
#include <fastgltf_util.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/normal.hpp>

namespace Luna {
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
struct EnableBitmaskOperators<VertexAttributeBits> : std::true_type {};

enum class MeshProcessingStepBits {
	UnpackVertices       = 1 << 1,
	GenerateFlatNormals  = 1 << 2,
	GenerateTangentSpace = 1 << 3,
	WeldVertices         = 1 << 4
};
using MeshProcessingSteps = Luna::Bitmask<MeshProcessingStepBits>;
template <>
struct EnableBitmaskOperators<MeshProcessingStepBits> : std::true_type {};

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

template <class... Ts>
struct Overloaded : Ts... {
	using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

struct Buffer {
	std::vector<uint8_t> Data;
};

struct GltfContext {
	Path SourcePath;
	Path GltfPath;
	Path GltfFolder;
	Path AssetFolder;
	std::unique_ptr<fastgltf::Asset> Asset;
	std::vector<Buffer> Buffers;
};

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

struct CombinedVertex {
	glm::vec3 Position;
	Vertex Attributes;

	bool operator==(const CombinedVertex& other) const {
		return Position == other.Position && Attributes == other.Attributes;
	}
};
}  // namespace Luna

template <>
struct std::hash<Luna::Vertex> {
	size_t operator()(const Luna::Vertex& v) {
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
template <>
struct std::hash<Luna::CombinedVertex> {
	size_t operator()(const Luna::CombinedVertex& v) const {
		Luna::Hasher h;
		h.Data(sizeof(v.Position), glm::value_ptr(v.Position));
		h(v.Attributes);

		return static_cast<size_t>(h.Get());
	}
};

namespace Luna {
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

static bool ParseGltf(GltfContext& context);
static bool LoadBuffers(GltfContext& context);
static bool LoadMeshes(GltfContext& context);

bool MeshGltfImporter::Import(const Path& sourcePath) {
	GltfContext context;
	context.SourcePath = sourcePath;
	context.GltfPath   = "project://" + sourcePath.String();
	context.GltfFolder = context.GltfPath.BaseDirectory();

	auto sourceFolder   = std::filesystem::path(sourcePath.String()).parent_path();
	context.AssetFolder = Path("Assets" / sourceFolder.lexically_relative("/Sources"));

	if (!ParseGltf(context)) { return false; }
	if (!LoadBuffers(context)) { return false; }
	if (!LoadMeshes(context)) { return false; }

	return true;
}

bool ParseGltf(GltfContext& context) {
	auto gltfMapping = Filesystem::OpenReadOnlyMapping(context.GltfPath);
	if (!gltfMapping) { return false; }

	fastgltf::GltfDataBuffer gltfData;
	gltfData.copyBytes(gltfMapping->Data<uint8_t>(), gltfMapping->GetSize());
	gltfMapping.Reset();

	fastgltf::GltfType gltfType = fastgltf::determineGltfFileType(&gltfData);
	if (gltfType == fastgltf::GltfType::Invalid) { return false; }

	fastgltf::Parser parser;
	const fastgltf::Options options = fastgltf::Options::LoadGLBBuffers | fastgltf::Options::DecomposeNodeMatrices;
	std::unique_ptr<fastgltf::glTF> loaded;
	if (gltfType == fastgltf::GltfType::glTF) {
		loaded = parser.loadGLTF(&gltfData, "", options);
	} else {
		loaded = parser.loadBinaryGLTF(&gltfData, "", options);
	}

	fastgltf::Error loadError = fastgltf::Error::None;
	loadError                 = parser.getError();
	if (loadError != fastgltf::Error::None) { return false; }
	loadError = loaded->parse(fastgltf::Category::All);
	if (loadError != fastgltf::Error::None) { return false; }
	loadError = loaded->validate();
	if (loadError != fastgltf::Error::None) { return false; }

	context.Asset = loaded->getParsedAsset();

	context.Buffers.resize(context.Asset->buffers.size());

	return true;
}

bool LoadBuffers(GltfContext& context) {
	const size_t bufferCount = context.Asset->buffers.size();

	for (size_t i = 0; i < bufferCount; ++i) {
		const auto& gltfBuffer = context.Asset->buffers[i];
		auto& buffer           = context.Buffers[i];

		std::visit(Overloaded{
								 [](auto& arg) {},
								 [&](const fastgltf::sources::Vector& vector) { buffer.Data = vector.bytes; },
								 [&](const fastgltf::sources::URI& uri) {
									 const Path path = context.GltfFolder / std::string(uri.uri.path());
									 auto map        = Filesystem::OpenReadOnlyMapping(path);
									 if (!map) { return; }
									 const uint8_t* dataStart = map->Data<uint8_t>() + uri.fileByteOffset;
									 buffer.Data              = {dataStart, dataStart + gltfBuffer.byteLength};
								 },
							 },
		           gltfBuffer.data);

		if (buffer.Data.size() == 0) { return false; }
	}

	return true;
}

bool LoadMeshes(GltfContext& context) {
	// Create a MikkTSpace context for tangent generation.
	SMikkTSpaceContext mikktContext = {.m_pInterface = &MikkTInterface};

	for (uint32_t i = 0; i < context.Asset->meshes.size(); ++i) {
		const auto& gltfMesh = context.Asset->meshes[i];

		auto meshName = "Mesh " + std::to_string(i);
		if (!gltfMesh.name.empty()) { meshName = gltfMesh.name; }
		auto mesh = AssetManager::CreateAsset<Mesh>(context.AssetFolder / "Meshes" / (meshName + ".lmesh"));

		const size_t defaultMaterialIndex               = context.Asset->materials.size();
		std::vector<fastgltf::Primitive> gltfPrimitives = gltfMesh.primitives;
		std::sort(gltfPrimitives.begin(),
		          gltfPrimitives.end(),
		          [defaultMaterialIndex](const fastgltf::Primitive& a, const fastgltf::Primitive& b) -> bool {
								return a.materialIndex.value_or(defaultMaterialIndex) > b.materialIndex.value_or(defaultMaterialIndex);
							});

		std::vector<std::vector<int>> materialPrimitives(context.Asset->materials.size());
		for (uint32_t i = 0; i < gltfPrimitives.size(); ++i) {
			const auto& gltfPrimitive = gltfPrimitives[i];
			materialPrimitives[gltfPrimitive.materialIndex.value_or(defaultMaterialIndex)].push_back(i);
		}
		materialPrimitives.erase(std::remove_if(materialPrimitives.begin(),
		                                        materialPrimitives.end(),
		                                        [](const std::vector<int>& material) { return material.empty(); }),
		                         materialPrimitives.end());

		std::vector<glm::vec3> meshPositions;
		std::vector<Vertex> meshVertices;
		std::vector<uint32_t> meshIndices;

		for (size_t materialIndex = 0; materialIndex < materialPrimitives.size(); ++materialIndex) {
			const auto& primitiveList = materialPrimitives[materialIndex];
			auto& submesh             = mesh->Submeshes.emplace_back();

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

				submesh.Bounds = Luna::AABB(boundsMin, boundsMax);
				submesh.VertexCount += positions.size();
				submesh.IndexCount += indices.size();
			}
		}

		const vk::DeviceSize positionSize = meshPositions.size() * sizeof(glm::vec3);
		const vk::DeviceSize vertexSize   = meshVertices.size() * sizeof(Vertex);
		const vk::DeviceSize indexSize    = meshIndices.size() * sizeof(uint32_t);

		mesh->BufferData.resize(positionSize + indexSize + vertexSize);
		memcpy(mesh->BufferData.data(), meshPositions.data(), positionSize);
		memcpy(mesh->BufferData.data() + positionSize, meshIndices.data(), indexSize);
		memcpy(mesh->BufferData.data() + positionSize + indexSize, meshVertices.data(), vertexSize);

		mesh->Bounds = AABB::Empty();
		for (const auto& submesh : mesh->Submeshes) { mesh->Bounds.Expand(submesh.Bounds); }
		mesh->TotalVertexCount = meshPositions.size();
		mesh->TotalIndexCount  = meshIndices.size();
		mesh->PositionSize     = positionSize + indexSize;
		mesh->AttributeSize    = vertexSize;

		AssetManager::SaveAsset(AssetManager::GetAssetMetadata(mesh->Handle), mesh);
	}

	return true;
}
}  // namespace Luna
