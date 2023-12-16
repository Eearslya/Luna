#include <meshoptimizer.h>

#include <Luna/Core/Filesystem.hpp>
#include <Luna/Core/Threading.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Renderer/Scene.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <fastgltf/parser.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/normal.hpp>
#include <stack>

namespace Luna {
template <class... Ts>
struct Overloaded : Ts... {
	using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

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

struct GltfBuffer {
	std::vector<uint8_t> Data;
};

struct GltfMesh {
	std::vector<glm::vec3> Positions;
	std::vector<Vertex> Attributes;
	std::vector<uint32_t> Indices;

	std::vector<meshopt_Meshlet> Meshlets;
	std::vector<uint32_t> MeshletIndices;
	std::vector<uint8_t> MeshletTriangles;
};

struct GltfContext {
	Path GltfFile;
	Path GltfFolder;
	fastgltf::Asset GltfAsset;

	std::vector<GltfBuffer> Buffers;
	std::vector<Mesh>& Meshes;
	std::vector<Scene::Node>& Nodes;
	std::vector<GltfMesh> RawMeshes;
	std::vector<Scene::Node*>& RootNodes;

	std::vector<glm::vec3>& Positions;
	std::vector<Vertex>& Attributes;
	std::vector<uint32_t>& Indices;
	std::vector<uint8_t>& Triangles;
};

template <typename Source, typename Destination>
static std::vector<Destination> ConvertAccessorData(const fastgltf::Asset& gltfAsset,
                                                    const std::vector<GltfBuffer>& buffers,
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
                                      const std::vector<GltfBuffer>& buffers,
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
                                      const std::vector<GltfBuffer>& buffers,
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

static void LoadBuffer(GltfContext& context, size_t bufferIndex) {
	const auto& gltfBuffer = context.GltfAsset.buffers[bufferIndex];
	auto& buffer           = context.Buffers[bufferIndex];
	std::visit(Overloaded{[](auto& arg) {},
	                      [&](const fastgltf::sources::Vector& vector) { buffer.Data = vector.bytes; },
	                      [&](const fastgltf::sources::ByteView& byteView) {
													buffer.Data = std::vector<uint8_t>(
														reinterpret_cast<const uint8_t*>(byteView.bytes.data()),
														reinterpret_cast<const uint8_t*>(byteView.bytes.data() + byteView.bytes.size()));
												},
	                      [&](const fastgltf::sources::URI& uri) {
													const Path path = context.GltfFolder / Path(uri.uri.string());
													auto map        = Filesystem::OpenReadOnlyMapping(path);
													if (!map) { return; }
													const uint8_t* dataStart = map->Data<uint8_t>() + uri.fileByteOffset;
													buffer.Data              = {dataStart, dataStart + gltfBuffer.byteLength};
												}},
	           gltfBuffer.data);
}

static void LoadMesh(GltfContext& context, size_t meshIndex) {
	const auto& gltfAsset = context.GltfAsset;
	const auto& gltfMesh  = gltfAsset.meshes[meshIndex];
	auto& rawMesh         = context.RawMeshes[meshIndex];

	// Start by sorting all of the primitives by their material.
	const size_t defaultMaterialIndex = gltfAsset.materials.size();
	std::vector<fastgltf::Primitive> gltfPrimitives(gltfMesh.primitives.begin(), gltfMesh.primitives.end());
	std::sort(gltfPrimitives.begin(),
	          gltfPrimitives.end(),
	          [defaultMaterialIndex](const fastgltf::Primitive& a, const fastgltf::Primitive& b) -> bool {
							return a.materialIndex.value_or(defaultMaterialIndex) > b.materialIndex.value_or(defaultMaterialIndex);
						});

	// Then, for each material, build a list of which primitives belong to each material.
	std::vector<std::vector<int>> materialPrimitives(gltfAsset.materials.size() + 1);
	for (uint32_t i = 0; i < gltfPrimitives.size(); ++i) {
		const auto& gltfPrimitive = gltfPrimitives[i];
		materialPrimitives[gltfPrimitive.materialIndex.value_or(defaultMaterialIndex)].push_back(i);
	}
	// Clean up any materials with no primitives.
	materialPrimitives.erase(std::remove_if(materialPrimitives.begin(),
	                                        materialPrimitives.end(),
	                                        [](const std::vector<int>& material) { return material.empty(); }),
	                         materialPrimitives.end());

	// We are now left with a list of materials used by this mesh, and which primitives use those materials.
	// We can use this information to merge primitives that use the same material into a single mesh.

	auto& meshPositions = rawMesh.Positions;
	auto& meshVertices  = rawMesh.Attributes;
	auto& meshIndices   = rawMesh.Indices;

	size_t vertexCount = 0;

	for (size_t materialIndex = 0; materialIndex < materialPrimitives.size(); ++materialIndex) {
		const auto& primitiveList = materialPrimitives[materialIndex];

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
				const auto& buffers = context.Buffers;

				positions      = GetAccessorData<glm::vec3>(gltfAsset, buffers, gltfPrimitive, VertexAttributeBits::Position);
				auto normals   = GetAccessorData<glm::vec3>(gltfAsset, buffers, gltfPrimitive, VertexAttributeBits::Normal);
				auto tangents  = GetAccessorData<glm::vec4>(gltfAsset, buffers, gltfPrimitive, VertexAttributeBits::Tangent);
				auto texcoord0 = GetAccessorData<glm::vec2>(gltfAsset, buffers, gltfPrimitive, VertexAttributeBits::Texcoord0);

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

			for (auto& i : indices) { i += vertexCount; }

			std::ranges::copy(positions, std::back_inserter(meshPositions));
			std::ranges::copy(vertices, std::back_inserter(meshVertices));
			std::ranges::copy(indices, std::back_inserter(meshIndices));

			vertexCount += positions.size();
		}
	}
}

static void LoadNode(GltfContext& context, size_t nodeIndex) {
	const auto& gltfNode = context.GltfAsset.nodes[nodeIndex];
	auto& node           = context.Nodes[nodeIndex];

	if (gltfNode.meshIndex.has_value()) { node.Mesh = &context.Meshes[gltfNode.meshIndex.value()]; }

	if (auto* trs = std::get_if<fastgltf::Node::TRS>(&gltfNode.transform)) {
		const auto t = glm::make_vec3(trs->translation.data());
		const auto s = glm::make_vec3(trs->scale.data());
		glm::quat r;
		r.x = trs->rotation[0];
		r.y = trs->rotation[1];
		r.z = trs->rotation[2];
		r.w = trs->rotation[3];

		node.Transform = glm::translate(glm::mat4(1.0f), t);
		node.Transform *= glm::mat4(r);
		node.Transform = glm::scale(node.Transform, s);
	} else if (auto* mat = std::get_if<fastgltf::Node::TransformMatrix>(&gltfNode.transform)) {
		node.Transform = glm::make_mat4(mat->data());
	}

	for (auto childIndex : gltfNode.children) {
		context.Nodes[childIndex].Parent = &node;
		node.Children.push_back(&context.Nodes[childIndex]);
	}
}

static void BuildMeshlets(GltfContext& context, size_t meshIndex) {
	auto& mesh = context.RawMeshes[meshIndex];

	constexpr static int MaxMeshletIndices   = 64;
	constexpr static int MaxMeshletTriangles = 64;
	const auto maxMeshlets = meshopt_buildMeshletsBound(mesh.Indices.size(), MaxMeshletIndices, MaxMeshletTriangles);

	auto& meshlets         = mesh.Meshlets;
	auto& meshletIndices   = mesh.MeshletIndices;
	auto& meshletTriangles = mesh.MeshletTriangles;
	meshlets.resize(maxMeshlets);
	meshletIndices.resize(maxMeshlets * MaxMeshletIndices);
	meshletTriangles.resize(maxMeshlets * MaxMeshletTriangles * 3);

	const auto meshletCount = meshopt_buildMeshlets(meshlets.data(),
	                                                meshletIndices.data(),
	                                                meshletTriangles.data(),
	                                                mesh.Indices.data(),
	                                                mesh.Indices.size(),
	                                                reinterpret_cast<const float*>(mesh.Positions.data()),
	                                                mesh.Positions.size(),
	                                                sizeof(glm::vec3),
	                                                MaxMeshletIndices,
	                                                MaxMeshletTriangles,
	                                                0.0f);
	meshlets.resize(meshletCount);

	const auto& lastMeshlet = meshlets.back();
	meshletIndices.resize(lastMeshlet.vertex_offset + lastMeshlet.vertex_count);
	meshletTriangles.resize(lastMeshlet.triangle_offset + ((lastMeshlet.triangle_count * 3 + 3) & ~3));
}

static void CombineMeshlets(GltfContext& context) {
	size_t vertexOffset   = context.Positions.size();
	size_t indexOffset    = context.Indices.size();
	size_t triangleOffset = context.Triangles.size();

	for (size_t meshIndex = 0; meshIndex < context.RawMeshes.size(); ++meshIndex) {
		auto& rawMesh                = context.RawMeshes[meshIndex];
		auto& mesh                   = context.Meshes[meshIndex];
		const auto& meshletIndices   = rawMesh.MeshletIndices;
		const auto& meshletTriangles = rawMesh.MeshletTriangles;

		for (const auto& meshlet : rawMesh.Meshlets) {
			const meshopt_Bounds bounds =
				meshopt_computeMeshletBounds(rawMesh.MeshletIndices.data() + meshlet.vertex_offset,
			                               rawMesh.MeshletTriangles.data() + meshlet.triangle_offset,
			                               meshlet.triangle_count,
			                               reinterpret_cast<float*>(rawMesh.Positions.data()),
			                               rawMesh.Positions.size(),
			                               sizeof(glm::vec3));

			mesh.Meshlets.emplace_back(Meshlet{.VertexOffset   = uint32_t(vertexOffset),
			                                   .IndexOffset    = uint32_t(indexOffset + meshlet.vertex_offset),
			                                   .TriangleOffset = uint32_t(triangleOffset + meshlet.triangle_offset),
			                                   .IndexCount     = uint32_t(meshlet.vertex_count),
			                                   .TriangleCount  = uint32_t(meshlet.triangle_count),
			                                   .BoundingSphere = glm::vec4(glm::make_vec3(bounds.center), bounds.radius)});
		}

		vertexOffset += rawMesh.Positions.size();
		indexOffset += rawMesh.MeshletIndices.size();
		triangleOffset += rawMesh.MeshletTriangles.size();

		std::ranges::move(rawMesh.Positions, std::back_inserter(context.Positions));
		std::ranges::move(rawMesh.Attributes, std::back_inserter(context.Attributes));
		std::ranges::move(rawMesh.MeshletIndices, std::back_inserter(context.Indices));
		std::ranges::move(rawMesh.MeshletTriangles, std::back_inserter(context.Triangles));
		break;
	}
}

glm::mat4 Scene::Node::GetGlobalTransform() const noexcept {
	if (Parent) {
		return Parent->GetGlobalTransform() * Transform;
	} else {
		return Transform;
	}
}

void Scene::Clear() {
	_meshes.clear();
	_nodes.clear();
	_rootNodes.clear();
	_positions.clear();
	_vertices.clear();
	_indices.clear();
	_triangles.clear();
	_positionBuffer.Reset();
	_vertexBuffer.Reset();
	_indexBuffer.Reset();
	_triangleBuffer.Reset();
}

RenderScene Scene::Flatten() const {
	RenderScene scene = {};

	const std::function<void(const Node&)> AddNode = [&](const Node& node) {
		if (node.Mesh && !node.Mesh->Meshlets.empty()) {
			const auto instanceId = scene.Transforms.size();
			scene.Transforms.emplace_back(node.GetGlobalTransform());
			for (auto& meshlet : node.Mesh->Meshlets) {
				meshlet.InstanceID = instanceId;
				scene.TriangleCount += meshlet.TriangleCount;
				scene.Meshlets.push_back(meshlet);
			}
		}

		for (const auto* child : node.Children) { AddNode(*child); }
	};
	for (auto* node : _rootNodes) { AddNode(*node); }

	return scene;
}

void Scene::LoadModel(const Path& gltfFile) {
	GltfContext context = {.GltfFile   = gltfFile,
	                       .GltfFolder = gltfFile.ParentPath(),
	                       .GltfAsset  = fastgltf::Asset(),
	                       .Meshes     = _meshes,
	                       .Nodes      = _nodes,
	                       .RootNodes  = _rootNodes,
	                       .Positions  = _positions,
	                       .Attributes = _vertices,
	                       .Indices    = _indices,
	                       .Triangles  = _triangles};

	if (!ParseGltf(context)) { return; }
	const auto& gltfAsset = context.GltfAsset;

	TaskComposer composer;

	auto& buffers = composer.BeginPipelineStage();
	for (size_t i = 0; i < gltfAsset.buffers.size(); ++i) {
		buffers.Enqueue([&context, i]() { LoadBuffer(context, i); });
	}

	auto& others = composer.BeginPipelineStage();
	for (size_t i = 0; i < gltfAsset.meshes.size(); ++i) {
		others.Enqueue([&context, i]() { LoadMesh(context, i); });
	}
	for (size_t i = 0; i < gltfAsset.nodes.size(); ++i) {
		others.Enqueue([&context, i]() { LoadNode(context, i); });
	}

	auto& meshlets = composer.BeginPipelineStage();
	for (size_t i = 0; i < gltfAsset.meshes.size(); ++i) {
		meshlets.Enqueue([&context, i]() { BuildMeshlets(context, i); });
	}

	auto& meshletCombine = composer.BeginPipelineStage();
	meshletCombine.Enqueue([&context]() { CombineMeshlets(context); });

	composer.GetOutgoingTask()->Wait();

	Vulkan::BufferCreateInfo positionCI(
		Vulkan::BufferDomain::Device, sizeof(glm::vec3) * _positions.size(), vk::BufferUsageFlagBits::eStorageBuffer);
	_positionBuffer = Renderer::GetDevice().CreateBuffer(positionCI, _positions.data());

	Vulkan::BufferCreateInfo vertexCI(
		Vulkan::BufferDomain::Device, sizeof(Vertex) * _vertices.size(), vk::BufferUsageFlagBits::eStorageBuffer);
	_vertexBuffer = Renderer::GetDevice().CreateBuffer(vertexCI, _vertices.data());

	Vulkan::BufferCreateInfo indexCI(
		Vulkan::BufferDomain::Device, sizeof(uint32_t) * _indices.size(), vk::BufferUsageFlagBits::eStorageBuffer);
	_indexBuffer = Renderer::GetDevice().CreateBuffer(indexCI, _indices.data());

	Vulkan::BufferCreateInfo triangleCI(
		Vulkan::BufferDomain::Device, sizeof(uint8_t) * _triangles.size(), vk::BufferUsageFlagBits::eStorageBuffer);
	_triangleBuffer = Renderer::GetDevice().CreateBuffer(triangleCI, _triangles.data());
}

bool Scene::ParseGltf(GltfContext& context) {
	fastgltf::Parser parser(fastgltf::Extensions::KHR_texture_basisu);

	// Load glTF/glb file.
	fastgltf::GltfDataBuffer gltfDataBuffer;
	{
		auto gltfFile = Filesystem::OpenReadOnlyMapping(context.GltfFile);
		gltfDataBuffer.copyBytes(gltfFile->Data<uint8_t>(), gltfFile->GetSize());
	}

	// Parse file into glTF asset.
	{
		const auto gltfFileType = fastgltf::determineGltfFileType(&gltfDataBuffer);
		if (gltfFileType == fastgltf::GltfType::glTF) {
			auto asset = parser.loadGLTF(&gltfDataBuffer, "", fastgltf::Options::None);
			if (asset.error() != fastgltf::Error::None) {
				Log::Error("Renderer", "Failed to load glTF: {}", fastgltf::getErrorMessage(asset.error()));
				return false;
			}
			context.GltfAsset = std::move(asset.get());
		} else {
			auto asset = parser.loadBinaryGLTF(&gltfDataBuffer, "", fastgltf::Options::LoadGLBBuffers);
			if (asset.error() != fastgltf::Error::None) {
				Log::Error("Renderer", "Failed to load glb: {}", fastgltf::getErrorMessage(asset.error()));
				return false;
			}
			context.GltfAsset = std::move(asset.get());
		}
	}

	context.Buffers.resize(context.GltfAsset.buffers.size());
	context.Meshes.resize(context.GltfAsset.meshes.size());
	context.Nodes.resize(context.GltfAsset.nodes.size());
	context.RawMeshes.resize(context.GltfAsset.meshes.size());

	const auto& gltfScene = context.GltfAsset.scenes[context.GltfAsset.defaultScene.value_or(0)];
	for (auto nodeIndex : gltfScene.nodeIndices) { context.RootNodes.push_back(&context.Nodes[nodeIndex]); }

	return true;
}
}  // namespace Luna
