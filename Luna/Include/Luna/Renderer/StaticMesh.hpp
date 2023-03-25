#pragma once

#include <Luna/Renderer/Common.hpp>
#include <Luna/Renderer/Material.hpp>
#include <Luna/Renderer/Renderable.hpp>
#include <Luna/Vulkan/Buffer.hpp>

namespace Luna {
constexpr static const int MaxStaticMeshInstances = 256;

class StaticMesh;

enum class MeshAttributeType { Position, Normal, Tangent, Texcoord0, Texcoord1, Bones0, Weights0, Color0 };
constexpr static const int MeshAttributeTypeCount = 8;

enum class MeshAttributeFlagBits {
	Position  = 1u << int(MeshAttributeType::Position),
	Normal    = 1u << int(MeshAttributeType::Position),
	Tangent   = 1u << int(MeshAttributeType::Position),
	Texcoord0 = 1u << int(MeshAttributeType::Position),
	Texcoord1 = 1u << int(MeshAttributeType::Position),
	Bones0    = 1u << int(MeshAttributeType::Position),
	Weights0  = 1u << int(MeshAttributeType::Position),
	Color0    = 1u << int(MeshAttributeType::Position)
};

struct MeshAttribute {
	vk::Format Format     = vk::Format::eUndefined;
	vk::DeviceSize Offset = 0;
};

struct StaticSubmeshRenderInfo {
	Vulkan::Program* Program = nullptr;
	uint32_t MaterialIndex   = 0;

	Vulkan::Buffer* PositionBuffer = nullptr;
	vk::DeviceSize PositionStride  = 0;
	vk::DeviceSize IndexOffset     = 0;
	vk::IndexType IndexType        = vk::IndexType::eUint32;

	Vulkan::Buffer* AttributeBuffer = nullptr;
	vk::DeviceSize AttributeStride  = 0;
	std::array<MeshAttribute, MeshAttributeTypeCount> Attributes;

	vk::DeviceSize VertexCount = 0;
	vk::DeviceSize IndexCount  = 0;
	vk::DeviceSize FirstVertex = 0;
	vk::DeviceSize FirstIndex  = 0;
};

struct StaticSubmeshInstanceInfo {
	glm::mat4 Model;
};

class StaticSubmesh : public Renderable {
 public:
	StaticSubmesh(StaticMesh* parent,
	              uint32_t materialIndex,
	              vk::DeviceSize vertexCount,
	              vk::DeviceSize indexCount,
	              vk::DeviceSize firstVertex,
	              vk::DeviceSize firstIndex);

	Hash GetInstanceKey() const;
	Hash GetBakedInstanceKey() const;

	void Bake();
	virtual void Enqueue(const RenderContext& context, const RenderableInfo& self, RenderQueue& queue) const override;
	virtual void Render(Vulkan::CommandBuffer& cmd) const override;

	uint32_t MaterialIndex     = 0;
	vk::DeviceSize VertexCount = 0;
	vk::DeviceSize IndexCount  = 0;
	vk::DeviceSize FirstVertex = 0;
	vk::DeviceSize FirstIndex  = 0;
	IntrusivePtr<Material> Material;

 private:
	StaticMesh* _parentMesh = nullptr;
	Hash _cachedHash        = 0;
};

class StaticMesh : public IntrusivePtrEnabled<StaticMesh> {
 public:
	void AddSubmesh(uint32_t materialIndex,
	                vk::DeviceSize vertexCount,
	                vk::DeviceSize indexCount,
	                vk::DeviceSize firstVertex,
	                vk::DeviceSize firstIndex);
	std::vector<IntrusivePtr<StaticSubmesh>> GatherOpaque() const;

	std::vector<IntrusivePtr<StaticSubmesh>> Submeshes;

	Vulkan::BufferHandle PositionBuffer;
	vk::DeviceSize PositionStride = 0;
	vk::DeviceSize IndexOffset    = 0;
	vk::IndexType IndexType       = vk::IndexType::eUint32;

	Vulkan::BufferHandle AttributeBuffer;
	vk::DeviceSize AttributeStride = 0;
	std::array<MeshAttribute, MeshAttributeTypeCount> Attributes;

	std::vector<IntrusivePtr<Material>> Materials;

 private:
};
}  // namespace Luna
