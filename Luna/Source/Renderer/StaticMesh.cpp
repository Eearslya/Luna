#include <Luna/Renderer/RenderQueue.hpp>
#include <Luna/Renderer/ShaderSuite.hpp>
#include <Luna/Renderer/StaticMesh.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>

namespace Luna {
static void RenderStaticSubmesh(Vulkan::CommandBuffer& cmd,
                                const RenderQueueData* renderInfos,
                                uint32_t instanceCount) {
	const auto& renderInfo = *static_cast<const StaticSubmeshRenderInfo*>(renderInfos[0].RenderInfo);

	cmd.SetProgram(renderInfo.Program);

	uint32_t toRender = 0;
	for (uint32_t i = 0; i < instanceCount; i += toRender) {
		toRender = std::min<uint32_t>(256, instanceCount - i);

		auto* instanceData = cmd.AllocateTypedUniformData<StaticSubmeshInstanceInfo>(1, 0, 256);
		for (uint32_t j = 0; j < toRender; ++j) {
			const auto& instanceInfo = *static_cast<const StaticSubmeshInstanceInfo*>(renderInfos[i + j].InstanceData);
			instanceData[j]          = instanceInfo;
		}

		cmd.Draw(3, toRender);
	}
}

void StaticSubmesh::Enqueue(const RenderContext& context, const RenderableInfo& self, RenderQueue& queue) const {
	auto* instanceInfo  = queue.AllocateOne<StaticSubmeshInstanceInfo>();
	instanceInfo->Model = self.Transform;

	auto* renderInfo =
		queue.Push<StaticSubmeshRenderInfo>(RenderQueueType::Opaque, 0, 0, RenderStaticSubmesh, instanceInfo);
	if (renderInfo) { renderInfo->Program = queue.GetShaderSuites()[int(RenderableType::Mesh)].GetProgram({}); }
}

std::vector<IntrusivePtr<StaticSubmesh>> StaticMesh::GatherOpaque() const {
	auto submesh = MakeHandle<StaticSubmesh>();

	return {submesh};
}
}  // namespace Luna
