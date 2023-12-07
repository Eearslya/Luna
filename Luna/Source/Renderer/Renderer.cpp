#include <imgui.h>

#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Filesystem.hpp>
#include <Luna/Core/Window.hpp>
#include <Luna/Core/WindowManager.hpp>
#include <Luna/Renderer/RenderGraph.hpp>
#include <Luna/Renderer/RenderPass.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Renderer/ShaderManager.hpp>
#include <Luna/Renderer/Swapchain.hpp>
#include <Luna/Renderer/UIManager.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/RenderPass.hpp>
#include <fastgltf/parser.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/normal.hpp>

template <class... Ts>
struct Overloaded : Ts... {
	using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

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
struct PushConstant {
	glm::mat4 Camera = glm::mat4(1.0f);
	glm::mat4 Model  = glm::mat4(1.0f);
};

struct ModelBuffer {
	std::vector<uint8_t> Data;
};

struct ModelPrimitive {
	vk::DeviceSize VertexCount = 0;
	vk::DeviceSize IndexCount  = 0;
	vk::DeviceSize FirstVertex = 0;
	vk::DeviceSize FirstIndex  = 0;
};

struct ModelMesh {
	std::vector<ModelPrimitive> Submeshes;
	vk::DeviceSize TotalVertexCount = 0;
	vk::DeviceSize TotalIndexCount  = 0;
	size_t PositionSize             = 0;
	size_t AttributeSize            = 0;

	Vulkan::BufferHandle PositionBuffer;
	Vulkan::BufferHandle AttributeBuffer;
};

struct ModelNode {
	ModelNode* Parent = nullptr;
	std::vector<ModelNode*> Children;
	ModelMesh* Mesh = nullptr;
};

struct Model {
	std::vector<ModelMesh> Meshes;
	std::vector<ModelNode> Nodes;

	std::vector<ModelNode*> RootNodes;
};

static struct RendererState {
	Vulkan::ContextHandle Context;
	Vulkan::DeviceHandle Device;
	Hash LastSwapchainHash = 0;

	RenderGraph Graph;

	glm::vec3 CameraPosition;
	glm::vec3 CameraTarget;
	Model Model;
	PushConstant PC;
	ShaderProgramVariant* Program;
} State;

template <typename Source, typename Destination>
static std::vector<Destination> ConvertAccessorData(const fastgltf::Asset& gltfAsset,
                                                    const std::vector<ModelBuffer>& buffers,
                                                    const fastgltf::Accessor& gltfAccessor,
                                                    bool vertexAccessor) {
	static_assert(AccessorType<Destination>::Count > 0, "Unknown type conversion given to ConvertAccessorData");
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
	const auto& gltfBufferView = gltfAsset.bufferViews[*gltfAccessor.bufferViewIndex];
	const auto& gltfBytes      = buffers[gltfBufferView.bufferIndex].Data;
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
static std::vector<T> GetAccessorData(const fastgltf::Asset& gltfAsset,
                                      const std::vector<ModelBuffer>& buffers,
                                      const fastgltf::Accessor& gltfAccessor,
                                      bool vertexAccessor = false) {
	constexpr auto outType          = AccessorType<T>::Type;
	constexpr auto outComponentType = AccessorType<T>::Component;
	const auto accessorType         = gltfAccessor.type;

	if (outType == accessorType) {
		switch (gltfAccessor.componentType) {
			case fastgltf::ComponentType::Byte:
				return ConvertAccessorData<int8_t, T>(gltfAsset, buffers, gltfAccessor, vertexAccessor);
			case fastgltf::ComponentType::UnsignedByte:
				return ConvertAccessorData<uint8_t, T>(gltfAsset, buffers, gltfAccessor, vertexAccessor);
			case fastgltf::ComponentType::Short:
				return ConvertAccessorData<int16_t, T>(gltfAsset, buffers, gltfAccessor, vertexAccessor);
			case fastgltf::ComponentType::UnsignedShort:
				return ConvertAccessorData<uint16_t, T>(gltfAsset, buffers, gltfAccessor, vertexAccessor);
			case fastgltf::ComponentType::UnsignedInt:
				return ConvertAccessorData<uint32_t, T>(gltfAsset, buffers, gltfAccessor, vertexAccessor);
			case fastgltf::ComponentType::Float:
				return ConvertAccessorData<float, T>(gltfAsset, buffers, gltfAccessor, vertexAccessor);
			case fastgltf::ComponentType::Double:
				return ConvertAccessorData<double, T>(gltfAsset, buffers, gltfAccessor, vertexAccessor);
			default:
				break;
		}
	}

	return {};
}

template <typename T>
static std::vector<T> GetAccessorData(const fastgltf::Asset& gltfAsset,
                                      const std::vector<ModelBuffer>& buffers,
                                      const fastgltf::Primitive& gltfPrimitive,
                                      VertexAttributeBits attribute) {
	if (attribute == VertexAttributeBits::Index) {
		if (gltfPrimitive.indicesAccessor.has_value()) {
			return GetAccessorData<T>(gltfAsset, buffers, gltfAsset.accessors[*gltfPrimitive.indicesAccessor]);
		}
	} else {
		const auto FindAttribute = [&](const char* attributeName) ->
			typename decltype(gltfPrimitive.attributes)::const_iterator {
				for (auto it = gltfPrimitive.attributes.begin(); it != gltfPrimitive.attributes.end(); ++it) {
					if (it->first == attributeName) { return it; }
				}

				return gltfPrimitive.attributes.end();
			};

		auto it = gltfPrimitive.attributes.end();
		switch (attribute) {
			case VertexAttributeBits::Position:
				it = FindAttribute("POSITION");
				break;
			case VertexAttributeBits::Normal:
				it = FindAttribute("NORMAL");
				break;
			case VertexAttributeBits::Tangent:
				it = FindAttribute("TANGENT");
				break;
			case VertexAttributeBits::Texcoord0:
				it = FindAttribute("TEXCOORD_0");
				break;
			case VertexAttributeBits::Texcoord1:
				it = FindAttribute("TEXCOORD_1");
				break;
			case VertexAttributeBits::Color0:
				it = FindAttribute("COLOR_0");
				break;
			case VertexAttributeBits::Joints0:
				it = FindAttribute("JOINTS_0");
				break;
			case VertexAttributeBits::Weights0:
				it = FindAttribute("WEIGHTS_0");
				break;
			default:
				throw std::runtime_error("Requested unknown vertex attribute!");
				break;
		}

		if (it != gltfPrimitive.attributes.end()) {
			return GetAccessorData<T>(gltfAsset, buffers, gltfAsset.accessors[it->second], true);
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

static void LoadModel() {
	fastgltf::Parser parser(fastgltf::Extensions::KHR_texture_basisu);

	// Load glTF/glb file.
	fastgltf::GltfDataBuffer gltfDataBuffer;
	{
		auto gltfFile = Filesystem::OpenReadOnlyMapping("res://Models/Bistro.glb");
		gltfDataBuffer.copyBytes(gltfFile->Data<uint8_t>(), gltfFile->GetSize());
	}

	// Parse file into glTF asset.
	fastgltf::Asset gltfAsset;
	{
		const auto gltfFileType = fastgltf::determineGltfFileType(&gltfDataBuffer);
		if (gltfFileType == fastgltf::GltfType::glTF) {
			auto asset = parser.loadGLTF(&gltfDataBuffer, "", fastgltf::Options::None);
			if (asset.error() != fastgltf::Error::None) {
				Log::Error("Renderer", "Failed to load glTF: {}", fastgltf::getErrorMessage(asset.error()));
				return;
			}
			gltfAsset = std::move(asset.get());
		} else {
			auto asset = parser.loadBinaryGLTF(&gltfDataBuffer, "", fastgltf::Options::LoadGLBBuffers);
			if (asset.error() != fastgltf::Error::None) {
				Log::Error("Renderer", "Failed to load glb: {}", fastgltf::getErrorMessage(asset.error()));
				return;
			}
			gltfAsset = std::move(asset.get());
		}
	}

	auto& model = State.Model;

	// Load Buffers.
	std::vector<ModelBuffer> buffers(gltfAsset.buffers.size());
	{
		for (size_t bufferIndex = 0; bufferIndex < gltfAsset.buffers.size(); ++bufferIndex) {
			const auto& gltfBuffer = gltfAsset.buffers[bufferIndex];
			auto& buffer           = buffers[bufferIndex];
			std::visit(Overloaded{[](auto& arg) {},
			                      [&](const fastgltf::sources::Vector& vector) { buffer.Data = vector.bytes; },
			                      [&](const fastgltf::sources::ByteView& byteView) {
															buffer.Data = std::vector<uint8_t>(
																reinterpret_cast<const uint8_t*>(byteView.bytes.data()),
																reinterpret_cast<const uint8_t*>(byteView.bytes.data() + byteView.bytes.size()));
														},
			                      [&](const fastgltf::sources::URI& uri) {}},
			           gltfBuffer.data);
		}
	}

	// Load Meshes.
	{
		model.Meshes.resize(gltfAsset.meshes.size());
		for (size_t meshIndex = 0; meshIndex < gltfAsset.meshes.size(); ++meshIndex) {
			const auto& gltfMesh = gltfAsset.meshes[meshIndex];
			auto& mesh           = model.Meshes[meshIndex];

			const size_t defaultMaterialIndex = gltfAsset.materials.size();
			std::vector<fastgltf::Primitive> gltfPrimitives(gltfMesh.primitives.begin(), gltfMesh.primitives.end());
			std::sort(gltfPrimitives.begin(),
			          gltfPrimitives.end(),
			          [defaultMaterialIndex](const fastgltf::Primitive& a, const fastgltf::Primitive& b) -> bool {
									return a.materialIndex.value_or(defaultMaterialIndex) >
				                 b.materialIndex.value_or(defaultMaterialIndex);
								});

			std::vector<std::vector<int>> materialPrimitives(gltfAsset.materials.size() + 1);
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
				auto& submesh             = mesh.Submeshes.emplace_back();

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
						positions    = GetAccessorData<glm::vec3>(gltfAsset, buffers, gltfPrimitive, VertexAttributeBits::Position);
						auto normals = GetAccessorData<glm::vec3>(gltfAsset, buffers, gltfPrimitive, VertexAttributeBits::Normal);
						auto tangents = GetAccessorData<glm::vec4>(gltfAsset, buffers, gltfPrimitive, VertexAttributeBits::Tangent);
						auto texcoord0 =
							GetAccessorData<glm::vec2>(gltfAsset, buffers, gltfPrimitive, VertexAttributeBits::Texcoord0);

						normals.resize(positions.size());
						tangents.resize(positions.size());
						texcoord0.resize(positions.size());

						vertices.reserve(positions.size());
						for (size_t i = 0; i < positions.size(); ++i) {
							vertices.push_back(Vertex{.Normal = normals[i], .Tangent = tangents[i], .Texcoord0 = texcoord0[i]});
						}

						indices = GetAccessorData<uint32_t>(gltfAsset, buffers, gltfPrimitive, VertexAttributeBits::Index);
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
						// MikkTContext context{positions, vertices};
						// mikktContext.m_pUserData = &context;
						// genTangSpaceDefault(&mikktContext);
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

			std::vector<uint8_t> bufferData(positionSize + indexSize + vertexSize);
			memcpy(bufferData.data(), meshPositions.data(), positionSize);
			memcpy(bufferData.data() + positionSize, meshIndices.data(), indexSize);
			memcpy(bufferData.data() + positionSize + indexSize, meshVertices.data(), vertexSize);

			const Vulkan::BufferCreateInfo bufferCI(
				Vulkan::BufferDomain::Device,
				positionSize + indexSize,
				vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eVertexBuffer);
			mesh.PositionBuffer = State.Device->CreateBuffer(bufferCI, bufferData.data());

			const Vulkan::BufferCreateInfo bufferCI2(
				Vulkan::BufferDomain::Device, vertexSize, vk::BufferUsageFlagBits::eVertexBuffer);
			mesh.AttributeBuffer = State.Device->CreateBuffer(bufferCI2, bufferData.data() + positionSize + indexSize);

			mesh.TotalVertexCount = meshPositions.size();
			mesh.TotalIndexCount  = meshIndices.size();
			mesh.PositionSize     = positionSize;
			mesh.AttributeSize    = vertexSize;
		}
	}

	// Load Nodes.
	{
		model.Nodes.resize(gltfAsset.nodes.size());
		for (size_t nodeIndex = 0; nodeIndex < gltfAsset.nodes.size(); ++nodeIndex) {
			const auto& gltfNode = gltfAsset.nodes[nodeIndex];
			auto& node           = model.Nodes[nodeIndex];

			if (gltfNode.meshIndex.has_value()) { node.Mesh = &model.Meshes[gltfNode.meshIndex.value()]; }

			for (auto childIndex : gltfNode.children) {
				model.Nodes[childIndex].Parent = &node;
				node.Children.push_back(&model.Nodes[childIndex]);
			}
		}
	}

	// Load Scene.
	{
		const auto& gltfScene = gltfAsset.scenes[gltfAsset.defaultScene.value_or(0)];
		for (auto nodeIndex : gltfScene.nodeIndices) { model.RootNodes.push_back(&model.Nodes[nodeIndex]); }
	}
}

static void RenderNode(const ModelNode& node, Vulkan::CommandBuffer& cmd) {
	if (node.Mesh) {
		const auto& mesh = *node.Mesh;
		cmd.SetVertexBinding(0, *mesh.PositionBuffer, 0, sizeof(glm::vec3), vk::VertexInputRate::eVertex);
		cmd.SetVertexBinding(1, *mesh.AttributeBuffer, 0, sizeof(Vertex), vk::VertexInputRate::eVertex);
		cmd.SetIndexBuffer(*mesh.PositionBuffer, mesh.PositionSize, vk::IndexType::eUint32);
		cmd.PushConstants(State.PC);

		for (const auto& submesh : mesh.Submeshes) {
			cmd.DrawIndexed(submesh.IndexCount, 1, submesh.FirstIndex, submesh.FirstVertex, 0);
		}
	}

	for (auto child : node.Children) { RenderNode(*child, cmd); }
}

static void RendererUI() {
	if (ImGui::Begin("Model")) {
		ImGui::DragFloat3("Position", glm::value_ptr(State.CameraPosition));
		ImGui::DragFloat3("Target", glm::value_ptr(State.CameraTarget));
		ImGui::Text("Meshes: %llu", State.Model.Meshes.size());
		ImGui::Text("Nodes: %llu", State.Model.Nodes.size());
	}
	ImGui::End();
}

bool Renderer::Initialize() {
	const auto instanceExtensions                   = WindowManager::GetRequiredInstanceExtensions();
	const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

	State.Context = MakeHandle<Vulkan::Context>(instanceExtensions, deviceExtensions);
	State.Device  = MakeHandle<Vulkan::Device>(*State.Context);

	LoadModel();
	State.CameraPosition = glm::vec3(-20, 10, -2);
	State.CameraTarget   = glm::vec3(0, 2, -2);

	return true;
}

void Renderer::Shutdown() {
	for (auto& mesh : State.Model.Meshes) {
		mesh.PositionBuffer.Reset();
		mesh.AttributeBuffer.Reset();
	}
	State.Graph.Reset();
	State.Device.Reset();
	State.Context.Reset();
}

Vulkan::Device& Renderer::GetDevice() {
	return *State.Device;
}

static void BakeRenderGraph() {
	// Preserve the RenderGraph buffer objects on the chance they don't need to be recreated.
	auto buffers = State.Graph.ConsumePhysicalBuffers();

	// Reset the RenderGraph state and proceed forward a frame to clean up the graph resources.
	State.Graph.Reset();
	State.Device->NextFrame();

	const auto swapchainExtent = Engine::GetMainWindow()->GetSwapchain().GetExtent();
	const auto swapchainFormat = Engine::GetMainWindow()->GetSwapchain().GetFormat();
	const ResourceDimensions backbufferDimensions{
		.Format = swapchainFormat, .Width = swapchainExtent.width, .Height = swapchainExtent.height};
	State.Graph.SetBackbufferDimensions(backbufferDimensions);

	State.Program =
		ShaderManager::RegisterGraphics("res://Shaders/StaticMesh.vert.glsl", "res://Shaders/StaticMesh.frag.glsl")
			->RegisterVariant();

	auto& pass = State.Graph.AddPass("Main");
	AttachmentInfo main;
	AttachmentInfo depth = main.Copy().SetFormat(State.Device->GetDefaultDepthFormat());
	pass.AddColorOutput("Final", main);
	pass.SetDepthStencilOutput("Depth", depth);
	pass.SetGetClearColor([](uint32_t, vk::ClearColorValue* value) -> bool {
		if (value) { *value = vk::ClearColorValue(0.36f, 0.0f, 0.63f, 1.0f); }
		return true;
	});
	pass.SetGetClearDepthStencil([](vk::ClearDepthStencilValue* value) -> bool {
		if (value) { *value = vk::ClearDepthStencilValue(1.0f, 0); }
		return true;
	});
	pass.SetBuildRenderPass([](Vulkan::CommandBuffer& cmd) {
		cmd.SetOpaqueState();
		cmd.SetProgram(State.Program->GetProgram());
		cmd.SetCullMode(vk::CullModeFlagBits::eBack);
		cmd.SetVertexAttribute(0, 0, vk::Format::eR32G32B32Sfloat, 0);
		cmd.SetVertexAttribute(1, 1, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, Normal));
		cmd.SetVertexAttribute(2, 1, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, Tangent));
		cmd.SetVertexAttribute(3, 1, vk::Format::eR32G32Sfloat, offsetof(Vertex, Texcoord0));
		cmd.SetVertexAttribute(4, 1, vk::Format::eR32G32Sfloat, offsetof(Vertex, Texcoord1));
		for (auto rootNode : State.Model.RootNodes) { RenderNode(*rootNode, cmd); }
	});

	{
		Luna::AttachmentInfo uiColor;

		auto& ui = State.Graph.AddPass("UI");

		ui.AddColorOutput("UI", uiColor, "Final");

		ui.SetGetClearColor([](uint32_t, vk::ClearColorValue* value) -> bool {
			if (value) { *value = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f); }
			return false;
		});
		ui.SetBuildRenderPass([](Vulkan::CommandBuffer& cmd) { UIManager::Render(cmd); });
	}

	State.Graph.SetBackbufferSource("UI");
	State.Graph.Bake(*State.Device);
	State.Graph.InstallPhysicalBuffers(buffers);

	State.Graph.Log();
}

void Renderer::Render() {
	auto& device = *State.Device;
	device.NextFrame();

	if (!Engine::GetMainWindow()) { return; }

	const bool acquired = Engine::GetMainWindow()->GetSwapchain().Acquire();
	if (!acquired) { return; }

	if (Engine::GetMainWindow()->GetSwapchainHash() != State.LastSwapchainHash) {
		BakeRenderGraph();
		State.LastSwapchainHash = Engine::GetMainWindow()->GetSwapchainHash();
	}

	RendererUI();
	const auto swapchainExtent = Engine::GetMainWindow()->GetSwapchain().GetExtent();
	const glm::mat4 projection =
		glm::perspective(glm::radians(70.0f), float(swapchainExtent.width) / float(swapchainExtent.height), 0.01f, 1000.0f);
	const glm::mat4 view = glm::lookAt(State.CameraPosition, State.CameraTarget, glm::vec3(0, 1, 0));
	State.PC.Camera      = projection * view;

	TaskComposer composer;
	State.Graph.SetupAttachments(*State.Device, &State.Device->GetSwapchainView());
	State.Graph.EnqueueRenderPasses(*State.Device, composer);
	composer.GetOutgoingTask()->Wait();

	Engine::GetMainWindow()->GetSwapchain().Present();
}
}  // namespace Luna
