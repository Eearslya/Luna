#include <Luna/Assets/AssetManager.hpp>
#include <Luna/Assets/Material.hpp>
#include <Luna/Assets/Texture.hpp>
#include <Luna/Renderer/RenderQueue.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Renderer/ShaderSuite.hpp>
#include <Luna/Renderer/StaticMesh.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>

namespace Luna {
struct StaticMeshRenderInfo {
	Vulkan::Program* Program              = nullptr;
	const Vulkan::Buffer* PositionBuffer  = nullptr;
	const Vulkan::Buffer* AttributeBuffer = nullptr;
	vk::DeviceSize IndexOffset            = 0;
	vk::DeviceSize VertexCount            = 0;
	vk::DeviceSize IndexCount             = 0;
	vk::DeviceSize FirstVertex            = 0;
	vk::DeviceSize FirstIndex             = 0;
	Material::MaterialData MaterialData   = {};
	const Vulkan::ImageView* Albedo       = nullptr;
	const Vulkan::ImageView* Normal       = nullptr;
	const Vulkan::ImageView* PBR          = nullptr;
	const Vulkan::ImageView* Emissive     = nullptr;
};

struct StaticMeshInstanceInfo {
	glm::mat4 Transform;
};

static void RenderStaticMesh(Vulkan::CommandBuffer& cmd, const RenderQueueData* renderInfos, uint32_t instanceCount) {
	const auto& renderInfo = *static_cast<const StaticMeshRenderInfo*>(renderInfos[0].RenderInfo);

	cmd.SetProgram(renderInfo.Program);
	cmd.SetVertexAttribute(0, 0, vk::Format::eR32G32B32Sfloat, 0);
	cmd.SetVertexAttribute(1, 1, vk::Format::eR32G32B32Sfloat, offsetof(Mesh::Vertex, Normal));
	cmd.SetVertexAttribute(2, 1, vk::Format::eR32G32B32A32Sfloat, offsetof(Mesh::Vertex, Tangent));
	cmd.SetVertexAttribute(3, 1, vk::Format::eR32G32Sfloat, offsetof(Mesh::Vertex, Texcoord0));
	cmd.SetVertexAttribute(4, 1, vk::Format::eR32G32Sfloat, offsetof(Mesh::Vertex, Texcoord1));
	cmd.SetVertexBinding(0, *renderInfo.PositionBuffer, 0, 12, vk::VertexInputRate::eVertex);
	cmd.SetVertexBinding(1, *renderInfo.AttributeBuffer, 0, sizeof(Mesh::Vertex), vk::VertexInputRate::eVertex);
	cmd.SetIndexBuffer(*renderInfo.PositionBuffer, renderInfo.IndexOffset, vk::IndexType::eUint32);

	constexpr static const int MaxInstances = 128;

	uint32_t toRender = 0;
	for (uint32_t i = 0; i < instanceCount; i += toRender) {
		toRender = std::min<uint32_t>(MaxInstances, instanceCount - i);

		auto* instanceData = cmd.AllocateTypedUniformData<StaticMeshInstanceInfo>(1, 0, toRender);
		for (uint32_t j = 0; j < toRender; ++j) {
			const auto& instanceInfo = *static_cast<const StaticMeshInstanceInfo*>(renderInfos[i + j].InstanceData);
			instanceData[j]          = instanceInfo;
		}

		auto* materialData = cmd.AllocateTypedUniformData<Material::MaterialData>(1, 1, 1);
		*materialData      = renderInfo.MaterialData;

		cmd.SetSrgbTexture(1, 2, *renderInfo.Albedo, Vulkan::StockSampler::DefaultGeometryFilterWrap);
		cmd.SetUnormTexture(1, 3, *renderInfo.Normal, Vulkan::StockSampler::DefaultGeometryFilterWrap);
		cmd.SetUnormTexture(1, 4, *renderInfo.PBR, Vulkan::StockSampler::DefaultGeometryFilterWrap);
		cmd.SetSrgbTexture(1, 5, *renderInfo.Emissive, Vulkan::StockSampler::DefaultGeometryFilterWrap);

		cmd.DrawIndexed(renderInfo.IndexCount, toRender, renderInfo.FirstIndex, renderInfo.FirstVertex);
	}
}

StaticMesh::StaticMesh(IntrusivePtr<Mesh> mesh, uint32_t submeshIndex, IntrusivePtr<Material> material)
		: _mesh(mesh), _submeshIndex(submeshIndex), _material(material) {
	Hasher h;
	h(_mesh->PositionBuffer->GetCookie());
	h(_mesh->AttributeBuffer->GetCookie());
	h(_mesh->TotalVertexCount);
	h(_mesh->TotalIndexCount);
	if (_material) {
		h(_material->Handle);
	} else {
		h(uint64_t(0));
	}
	h(_mesh->Submeshes[_submeshIndex].VertexCount);
	h(_mesh->Submeshes[_submeshIndex].IndexCount);
	h(_mesh->Submeshes[_submeshIndex].FirstVertex);
	h(_mesh->Submeshes[_submeshIndex].FirstIndex);
	_instanceKey = h.Get();
}

void StaticMesh::Enqueue(const RenderContext& context, const RenderableInfo& self, RenderQueue& queue) const {
	RenderQueueType queueType = RenderQueueType::Opaque;

	auto* instanceInfo      = queue.AllocateOne<StaticMeshInstanceInfo>();
	instanceInfo->Transform = self.Transform;

	auto* renderInfo = queue.Push<StaticMeshRenderInfo>(queueType, _instanceKey, 0, RenderStaticMesh, instanceInfo);
	if (renderInfo) {
		renderInfo->Program         = queue.GetShaderSuites()[int(RenderableType::Mesh)].GetProgram({});
		renderInfo->PositionBuffer  = _mesh->PositionBuffer.Get();
		renderInfo->AttributeBuffer = _mesh->AttributeBuffer.Get();
		renderInfo->IndexOffset     = _mesh->TotalVertexCount * 12;
		renderInfo->VertexCount     = _mesh->Submeshes[_submeshIndex].VertexCount;
		renderInfo->IndexCount      = _mesh->Submeshes[_submeshIndex].IndexCount;
		renderInfo->FirstVertex     = _mesh->Submeshes[_submeshIndex].FirstVertex;
		renderInfo->FirstIndex      = _mesh->Submeshes[_submeshIndex].FirstIndex;

		const auto MatTexture = [&](AssetHandle textureAsset,
		                            const Vulkan::ImageHandle& placeholder) -> const Vulkan::ImageView* {
			auto texture = AssetManager::GetAsset<Texture>(textureAsset, true);
			if (!texture) { return &placeholder->GetView(); }
			return &texture->Image->GetView();
		};

		const auto& def = Renderer::GetDefaultImages();

		if (_material) {
			renderInfo->MaterialData = _material->Data();
			renderInfo->Albedo       = MatTexture(_material->Albedo, def.White2D);
			renderInfo->Normal       = MatTexture(_material->Normal, def.Normal2D);
			renderInfo->PBR          = MatTexture(_material->PBR, def.White2D);
			renderInfo->Emissive     = MatTexture(_material->Emissive, def.White2D);
		} else {
			renderInfo->MaterialData = Material::MaterialData{};
			renderInfo->Albedo       = &def.White2D->GetView();
			renderInfo->Normal       = &def.Normal2D->GetView();
			renderInfo->PBR          = &def.White2D->GetView();
			renderInfo->Emissive     = &def.White2D->GetView();
		}
	}
}

void StaticMesh::Render(Vulkan::CommandBuffer& cmd) const {}
}  // namespace Luna
