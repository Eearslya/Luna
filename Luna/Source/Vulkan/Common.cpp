#define VMA_IMPLEMENTATION

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include <Luna/Vulkan/Common.hpp>
#pragma clang diagnostic pop

namespace Luna {
namespace Vulkan {
vk::AccessFlags DowngradeAccessFlags2(vk::AccessFlags2 access2) {
	constexpr static const vk::AccessFlags2 baseAccess =
		vk::AccessFlagBits2::eIndirectCommandRead | vk::AccessFlagBits2::eIndexRead |
		vk::AccessFlagBits2::eVertexAttributeRead | vk::AccessFlagBits2::eUniformRead |
		vk::AccessFlagBits2::eInputAttachmentRead | vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite |
		vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite |
		vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite |
		vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eHostRead |
		vk::AccessFlagBits2::eHostWrite | vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite |
		vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eAccelerationStructureWriteKHR;

	constexpr static const vk::AccessFlags2 shaderRead =
		vk::AccessFlagBits2::eShaderSampledRead | vk::AccessFlagBits2::eShaderStorageRead;

	constexpr static const vk::AccessFlags2 shaderWrite = vk::AccessFlagBits2::eShaderStorageWrite;

	vk::AccessFlags access1(uint32_t(uint64_t(access2 & baseAccess)));

	if (access2 & shaderRead) { access1 |= vk::AccessFlagBits::eShaderRead; }
	if (access2 & shaderWrite) { access1 |= vk::AccessFlagBits::eShaderWrite; }

	return access1;
}

vk::PipelineStageFlags DowngradePipelineStageFlags2(vk::PipelineStageFlags2 stage2) {
	constexpr static const vk::PipelineStageFlags2 baseStages =
		vk::PipelineStageFlagBits2::eTopOfPipe | vk::PipelineStageFlagBits2::eDrawIndirect |
		vk::PipelineStageFlagBits2::eVertexInput | vk::PipelineStageFlagBits2::eVertexShader |
		vk::PipelineStageFlagBits2::eTessellationControlShader | vk::PipelineStageFlagBits2::eTessellationEvaluationShader |
		vk::PipelineStageFlagBits2::eGeometryShader | vk::PipelineStageFlagBits2::eFragmentShader |
		vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests |
		vk::PipelineStageFlagBits2::eColorAttachmentOutput | vk::PipelineStageFlagBits2::eComputeShader |
		vk::PipelineStageFlagBits2::eTransfer | vk::PipelineStageFlagBits2::eBottomOfPipe |
		vk::PipelineStageFlagBits2::eHost | vk::PipelineStageFlagBits2::eAllGraphics |
		vk::PipelineStageFlagBits2::eAllCommands | vk::PipelineStageFlagBits2::eTransformFeedbackEXT |
		vk::PipelineStageFlagBits2::eConditionalRenderingEXT | vk::PipelineStageFlagBits2::eRayTracingShaderKHR |
		vk::PipelineStageFlagBits2::eFragmentShadingRateAttachmentKHR | vk::PipelineStageFlagBits2::eCommandPreprocessNV |
		vk::PipelineStageFlagBits2::eTaskShaderEXT | vk::PipelineStageFlagBits2::eMeshShaderEXT;

	constexpr static const vk::PipelineStageFlags2 transferStages =
		vk::PipelineStageFlagBits2::eCopy | vk::PipelineStageFlagBits2::eBlit | vk::PipelineStageFlagBits2::eResolve |
		vk::PipelineStageFlagBits2::eClear | vk::PipelineStageFlagBits2::eAccelerationStructureCopyKHR;

	constexpr static const vk::PipelineStageFlags2 vertexStages =
		vk::PipelineStageFlagBits2::eIndexInput | vk::PipelineStageFlagBits2::eVertexAttributeInput;

	vk::PipelineStageFlags stage1(uint32_t(uint64_t(stage2 & baseStages)));

	if (stage2 & transferStages) { stage1 |= vk::PipelineStageFlagBits::eTransfer; }
	if (stage2 & vertexStages) { stage1 |= vk::PipelineStageFlagBits::eVertexInput; }
	if (stage2 & vk::PipelineStageFlagBits2::ePreRasterizationShaders) {
		stage1 |= vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eTessellationControlShader |
		          vk::PipelineStageFlagBits::eTessellationEvaluationShader | vk::PipelineStageFlagBits::eGeometryShader |
		          vk::PipelineStageFlagBits::eTaskShaderEXT | vk::PipelineStageFlagBits::eMeshShaderEXT;
	}

	return stage1;
}

vk::PipelineStageFlags DowngradeDstPipelineStageFlags2(vk::PipelineStageFlags2 stage2) {
	if (stage2 == vk::PipelineStageFlagBits2::eNone) { return vk::PipelineStageFlagBits::eBottomOfPipe; }

	return DowngradePipelineStageFlags2(stage2);
}

vk::PipelineStageFlags DowngradeSrcPipelineStageFlags2(vk::PipelineStageFlags2 stage2) {
	if (stage2 == vk::PipelineStageFlagBits2::eNone) { return vk::PipelineStageFlagBits::eTopOfPipe; }

	return DowngradePipelineStageFlags2(stage2);
}
}  // namespace Vulkan
}  // namespace Luna
