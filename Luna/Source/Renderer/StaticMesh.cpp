#include <Luna/Renderer/RenderQueue.hpp>
#include <Luna/Renderer/ShaderSuite.hpp>
#include <Luna/Renderer/StaticMesh.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Image.hpp>

namespace Luna {
static void RenderStaticSubmesh(Vulkan::CommandBuffer& cmd,
                                const RenderQueueData* renderInfos,
                                uint32_t instanceCount) {
	const auto& renderInfo = *static_cast<const StaticSubmeshRenderInfo*>(renderInfos[0].RenderInfo);

	cmd.SetProgram(renderInfo.Program);

	cmd.SetVertexBinding(0, *renderInfo.PositionBuffer, 0, renderInfo.PositionStride, vk::VertexInputRate::eVertex);
	if (renderInfo.IndexCount > 0) {
		cmd.SetIndexBuffer(*renderInfo.PositionBuffer, renderInfo.IndexOffset, renderInfo.IndexType);
	}
	if (renderInfo.AttributeBuffer) {
		cmd.SetVertexBinding(1, *renderInfo.AttributeBuffer, 0, renderInfo.AttributeStride, vk::VertexInputRate::eVertex);
	}

	for (uint32_t i = 0; i < MeshAttributeTypeCount; ++i) {
		if (renderInfo.Attributes[i].Format != vk::Format::eUndefined) {
			cmd.SetVertexAttribute(i, i == 0 ? 0 : 1, renderInfo.Attributes[i].Format, renderInfo.Attributes[i].Offset);
		}
	}

	uint32_t toRender = 0;
	for (uint32_t i = 0; i < instanceCount; i += toRender) {
		toRender = std::min<uint32_t>(MaxStaticMeshInstances, instanceCount - i);

		auto* instanceData = cmd.AllocateTypedUniformData<StaticSubmeshInstanceInfo>(1, 0, toRender);
		for (uint32_t j = 0; j < toRender; ++j) {
			const auto& instanceInfo = *static_cast<const StaticSubmeshInstanceInfo*>(renderInfos[i + j].InstanceData);
			instanceData[j]          = instanceInfo;
		}

		if (renderInfo.IndexCount > 0) {
			cmd.DrawIndexed(renderInfo.IndexCount, toRender, renderInfo.FirstIndex, renderInfo.FirstVertex);
		} else {
			cmd.Draw(renderInfo.VertexCount, toRender, renderInfo.FirstVertex);
		}
	}
}

StaticSubmesh::StaticSubmesh(StaticMesh* parent,
                             uint32_t materialIndex,
                             vk::DeviceSize vertexCount,
                             vk::DeviceSize indexCount,
                             vk::DeviceSize firstVertex,
                             vk::DeviceSize firstIndex)
		: _parentMesh(parent),
			MaterialIndex(materialIndex),
			VertexCount(vertexCount),
			IndexCount(indexCount),
			FirstVertex(firstVertex),
			FirstIndex(firstIndex) {}

Hash StaticSubmesh::GetInstanceKey() const {
	Hasher h;
	h(_parentMesh->PositionBuffer->GetCookie());
	h(_parentMesh->PositionStride);
	h(_parentMesh->IndexOffset);
	h(_parentMesh->IndexType);

	if (_parentMesh->AttributeBuffer) {
		h(_parentMesh->AttributeBuffer->GetCookie());
		h(_parentMesh->AttributeStride);
	}

	for (const auto& attr : _parentMesh->Attributes) {
		h(attr.Format);
		h(attr.Offset);
	}

	h(MaterialIndex);
	h(VertexCount);
	h(IndexCount);
	h(FirstVertex);
	h(FirstIndex);

	return h.Get();
}

Hash StaticSubmesh::GetBakedInstanceKey() const {
	Hasher h(_cachedHash);

	return h.Get();
}

void StaticSubmesh::Bake() {
	_cachedHash = GetInstanceKey();
}

void StaticSubmesh::Enqueue(const RenderContext& context, const RenderableInfo& self, RenderQueue& queue) const {
	const auto instanceKey = GetBakedInstanceKey();

	auto* instanceInfo  = queue.AllocateOne<StaticSubmeshInstanceInfo>();
	instanceInfo->Model = self.Transform;

	auto* renderInfo =
		queue.Push<StaticSubmeshRenderInfo>(RenderQueueType::Opaque, instanceKey, 0, RenderStaticSubmesh, instanceInfo);
	if (renderInfo) {
		renderInfo->Program       = queue.GetShaderSuites()[int(RenderableType::Mesh)].GetProgram({});
		renderInfo->MaterialIndex = MaterialIndex;

		renderInfo->PositionBuffer = _parentMesh->PositionBuffer.Get();
		renderInfo->PositionStride = _parentMesh->PositionStride;
		renderInfo->IndexOffset    = _parentMesh->IndexOffset;
		renderInfo->IndexType      = _parentMesh->IndexType;

		renderInfo->AttributeBuffer = _parentMesh->AttributeBuffer.Get();
		renderInfo->AttributeStride = _parentMesh->AttributeStride;
		renderInfo->Attributes      = _parentMesh->Attributes;

		renderInfo->VertexCount = VertexCount;
		renderInfo->IndexCount  = IndexCount;
		renderInfo->FirstVertex = FirstVertex;
		renderInfo->FirstIndex  = FirstIndex;
	}
}

void StaticSubmesh::Render(Vulkan::CommandBuffer& cmd) const {}

void StaticMesh::AddSubmesh(uint32_t materialIndex,
                            vk::DeviceSize vertexCount,
                            vk::DeviceSize indexCount,
                            vk::DeviceSize firstVertex,
                            vk::DeviceSize firstIndex) {
	auto submesh = MakeHandle<StaticSubmesh>(this, materialIndex, vertexCount, indexCount, firstVertex, firstIndex);
	submesh->Bake();
	Submeshes.push_back(std::move(submesh));
}

std::vector<IntrusivePtr<StaticSubmesh>> StaticMesh::GatherOpaque() const {
	return Submeshes;
}
}  // namespace Luna
