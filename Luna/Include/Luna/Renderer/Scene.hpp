#pragma once

#include <Luna/Renderer/Common.hpp>
#include <Luna/Utility/Path.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Luna {
struct GltfContext;

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

struct Meshlet {
	uint32_t VertexOffset;
	uint32_t IndexOffset;
	uint32_t TriangleOffset;
	uint32_t IndexCount;
	uint32_t TriangleCount;
	uint32_t InstanceID;
	glm::vec4 BoundingSphere;
};

struct Mesh {
	std::vector<Meshlet> Meshlets;
};

struct RenderScene {
	std::vector<Meshlet> Meshlets;
	std::vector<glm::mat4> Transforms;
	uint64_t TriangleCount = 0;
};

class Scene {
 public:
	struct Node {
		[[nodiscard]] glm::mat4 GetGlobalTransform() const noexcept;

		Node* Parent = nullptr;
		std::vector<Node*> Children;
		glm::mat4 Transform = glm::mat4(1.0f);

		Mesh* Mesh = nullptr;
	};

	[[nodiscard]] Vulkan::Buffer& GetPositionBuffer() {
		return *_positionBuffer;
	}
	[[nodiscard]] Vulkan::Buffer& GetVertexBuffer() {
		return *_vertexBuffer;
	}
	[[nodiscard]] Vulkan::Buffer& GetIndexBuffer() {
		return *_indexBuffer;
	}
	[[nodiscard]] Vulkan::Buffer& GetTriangleBuffer() {
		return *_triangleBuffer;
	}

	void Clear();
	RenderScene Flatten() const;
	void LoadModel(const Path& gltfFile);

 private:
	bool ParseGltf(GltfContext& context);

	std::vector<Mesh> _meshes;
	std::vector<Node> _nodes;
	std::vector<Node*> _rootNodes;

	std::vector<glm::vec3> _positions;
	std::vector<Vertex> _vertices;
	std::vector<uint32_t> _indices;
	std::vector<uint8_t> _triangles;

	Vulkan::BufferHandle _positionBuffer;
	Vulkan::BufferHandle _vertexBuffer;
	Vulkan::BufferHandle _indexBuffer;
	Vulkan::BufferHandle _triangleBuffer;
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
