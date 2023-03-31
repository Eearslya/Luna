#include "GBufferRenderer.hpp"

#include <Luna/Renderer/Environment.hpp>
#include <Luna/Renderer/RenderContext.hpp>
#include <Luna/Renderer/StaticMesh.hpp>
#include <Luna/Scene/Entity.hpp>
#include <Luna/Scene/MeshRendererComponent.hpp>
#include <Luna/Scene/Scene.hpp>
#include <Luna/Scene/SkyLightComponent.hpp>
#include <Luna/Scene/TransformComponent.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Image.hpp>

struct ObjectData {
	ObjectData() = default;
	ObjectData(const glm::mat4& transform, vk::DeviceAddress posBuffer, vk::DeviceAddress attrBuffer)
			: Transform(transform), PositionBuffer(posBuffer), AttributeBuffer(attrBuffer) {}

	glm::mat4 Transform;
	vk::DeviceAddress PositionBuffer;
	vk::DeviceAddress AttributeBuffer;
	uint32_t MaterialIndex;
};

GBufferRenderer::GBufferRenderer(Luna::RenderContext& context, Luna::Scene& scene)
		: _context(context), _scene(scene), _renderScene(scene) {
	_materialBuffers.resize(_context.GetFrameContextCount());
	_objectBuffers.resize(_context.GetFrameContextCount());
	_indirectBuffers.resize(_context.GetFrameContextCount());
}

bool GBufferRenderer::GetClearColor(uint32_t attachment, vk::ClearColorValue* value) const {
	if (value) { *value = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f); }
	return true;
}

void GBufferRenderer::BuildRenderPass(Luna::Vulkan::CommandBuffer& cmd) {
	Luna::RenderParameters* params = cmd.AllocateTypedUniformData<Luna::RenderParameters>(0, 0, 1);
	*params                        = _context.GetRenderParameters();

	cmd.SetBindless(1, _context.GetBindlessSet());

	RenderMeshes(cmd);
}

void GBufferRenderer::EnqueuePrepareRenderPass(Luna::RenderGraph& graph, Luna::TaskComposer& composer) {}

void GBufferRenderer::RenderMeshes(Luna::Vulkan::CommandBuffer& cmd) {
	const auto& registry = _scene.GetRegistry();

	std::vector<uint32_t> indices;
	uint32_t nextMaterialIndex = 0;
	std::unordered_map<Luna::MaterialData, uint32_t> materials;
	std::vector<ObjectData> objects;
	std::vector<vk::DrawIndexedIndirectCommand> draws;

	auto renderables = registry.view<Luna::MeshRendererComponent>();
	for (auto entityId : renderables) {
		auto [cMeshRenderer] = renderables.get(entityId);
		if (!cMeshRenderer.StaticMesh) { continue; }

		const auto& mesh = *cMeshRenderer.StaticMesh;
		const Luna::Entity entity(entityId, _scene);
		const auto transform = entity.GetGlobalTransform();

		ObjectData object(transform, mesh.PositionBuffer->GetDeviceAddress(), mesh.AttributeBuffer->GetDeviceAddress());

		const vk::DeviceSize firstIndex = indices.size();
		indices.insert(indices.end(), mesh.Indices.begin(), mesh.Indices.end());

		const auto submeshes = mesh.GatherOpaque();
		for (const auto& submesh : submeshes) {
			const auto& material         = mesh.Materials[submesh->MaterialIndex];
			const auto materialData      = material->Data(_context);
			const auto [it, newMaterial] = materials.insert({materialData, nextMaterialIndex});
			if (newMaterial) {
				object.MaterialIndex = nextMaterialIndex++;
			} else {
				object.MaterialIndex = it->second;
			}
			objects.push_back(object);
			draws.emplace_back(submesh->IndexCount, 1, firstIndex + submesh->FirstIndex, submesh->FirstVertex, draws.size());
		}
	}

	if (draws.size() == 0) { return; }

	uint32_t* indexBuffer = cmd.AllocateTypedIndexData<uint32_t>(indices.size());
	memcpy(indexBuffer, indices.data(), indices.size() * sizeof(uint32_t));

	std::vector<Luna::MaterialData> orderedMaterials(materials.size());
	for (const auto& [data, index] : materials) { orderedMaterials[index] = data; }

	const auto frameIndex = _context.GetFrameIndex();
	auto& materialBuffer  = _materialBuffers[frameIndex];
	auto& objectBuffer    = _objectBuffers[frameIndex];
	auto& indirectBuffer  = _indirectBuffers[frameIndex];

	const vk::DeviceSize materialBufferSize = orderedMaterials.size() * sizeof(Luna::MaterialData);
	if (!materialBuffer || materialBuffer->GetCreateInfo().Size < materialBufferSize) {
		const Luna::Vulkan::BufferCreateInfo bufferCI(
			Luna::Vulkan::BufferDomain::Host, materialBufferSize, vk::BufferUsageFlagBits::eStorageBuffer);
		materialBuffer = _context.GetDevice().CreateBuffer(bufferCI);
	}

	const vk::DeviceSize objectBufferSize = objects.size() * sizeof(ObjectData);
	if (!objectBuffer || objectBuffer->GetCreateInfo().Size < objectBufferSize) {
		const Luna::Vulkan::BufferCreateInfo bufferCI(
			Luna::Vulkan::BufferDomain::Host, objectBufferSize, vk::BufferUsageFlagBits::eStorageBuffer);
		objectBuffer = _context.GetDevice().CreateBuffer(bufferCI);
	}

	const vk::DeviceSize indirectBufferSize = draws.size() * sizeof(vk::DrawIndexedIndirectCommand);
	if (!indirectBuffer || indirectBuffer->GetCreateInfo().Size < indirectBufferSize) {
		const Luna::Vulkan::BufferCreateInfo bufferCI(
			Luna::Vulkan::BufferDomain::Host, indirectBufferSize, vk::BufferUsageFlagBits::eIndirectBuffer);
		indirectBuffer = _context.GetDevice().CreateBuffer(bufferCI);
	}

	void* materialData = materialBuffer->Map();
	memcpy(materialData, orderedMaterials.data(), materialBufferSize);

	void* objectData = objectBuffer->Map();
	memcpy(objectData, objects.data(), objectBufferSize);

	void* indirectData = indirectBuffer->Map();
	memcpy(indirectData, draws.data(), indirectBufferSize);

	cmd.SetOpaqueState();

	cmd.SetStorageBuffer(2, 0, *materialBuffer);
	cmd.SetStorageBuffer(2, 1, *objectBuffer);

	cmd.SetProgram(_context.GetShaders().PBRGBuffer->GetProgram());
	cmd.DrawIndexedIndirect(*indirectBuffer, 0, draws.size(), sizeof(vk::DrawIndexedIndirectCommand));
}
