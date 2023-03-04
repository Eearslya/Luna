#include <Luna/Vulkan/Common.hpp>
#include <algorithm>
#include <sstream>

namespace Luna {
namespace Vulkan {
uint32_t CalculateMipLevels(uint32_t width, uint32_t height, uint32_t depth) {
	return std::floor(std::log2(std::max(std::max(width, height), depth))) + 1;
}

uint32_t CalculateMipLevels(vk::Extent2D extent) {
	return CalculateMipLevels(extent.width, extent.height, 1);
}

uint32_t CalculateMipLevels(vk::Extent3D extent) {
	return CalculateMipLevels(extent.width, extent.height, extent.depth);
}

vk::AccessFlags DowngradeAccessFlags2(vk::AccessFlags2 access) {
	constexpr vk::AccessFlags2 sampledFlags = vk::AccessFlagBits2::eShaderSampledRead |
	                                          vk::AccessFlagBits2::eShaderStorageRead |
	                                          vk::AccessFlagBits2::eShaderBindingTableReadKHR;
	constexpr vk::AccessFlags2 storageFlags = vk::AccessFlagBits2::eShaderStorageWrite;

	if (access & sampledFlags) {
		access |= vk::AccessFlagBits2::eShaderRead;
		access &= ~sampledFlags;
	}

	if (access & storageFlags) {
		access |= vk::AccessFlagBits2::eShaderWrite;
		access &= ~storageFlags;
	}

	return static_cast<vk::AccessFlags>(static_cast<VkAccessFlags2>(access));
}

vk::PipelineStageFlags DowngradeDstPipelineStageFlags2(vk::PipelineStageFlags2 stages2) {
	auto stages = DowngradePipelineStageFlags2(stages2);
	if (!bool(stages)) { stages = vk::PipelineStageFlagBits::eBottomOfPipe; }

	return stages;
}

vk::PipelineStageFlags DowngradePipelineStageFlags2(vk::PipelineStageFlags2 stages) {
	constexpr vk::PipelineStageFlags2 transferFlags =
		vk::PipelineStageFlagBits2::eCopy | vk::PipelineStageFlagBits2::eBlit | vk::PipelineStageFlagBits2::eResolve |
		vk::PipelineStageFlagBits2::eClear | vk::PipelineStageFlagBits2::eAccelerationStructureCopyKHR;
	constexpr vk::PipelineStageFlags2 prerasterFlags = vk::PipelineStageFlagBits2::ePreRasterizationShaders;

	if (stages & transferFlags) {
		stages |= vk::PipelineStageFlagBits2::eTransfer;
		stages &= ~transferFlags;
	}

	if (stages & prerasterFlags) {
		stages |= vk::PipelineStageFlagBits2::eVertexShader;
		stages &= ~prerasterFlags;
	}

	return static_cast<vk::PipelineStageFlags>(static_cast<VkPipelineStageFlags2>(stages));
}

vk::PipelineStageFlags DowngradeSrcPipelineStageFlags2(vk::PipelineStageFlags2 stages2) {
	auto stages = DowngradePipelineStageFlags2(stages2);
	if (!bool(stages)) { stages = vk::PipelineStageFlagBits::eTopOfPipe; }

	return stages;
}

std::string FormatSize(vk::DeviceSize size) {
	std::ostringstream oss;
	if (size < 1024) {
		oss << size << " B";
	} else if (size < 1024 * 1024) {
		oss << size / 1024.f << " KB";
	} else if (size < 1024 * 1024 * 1024) {
		oss << size / (1024.0f * 1024.0f) << " MB";
	} else {
		oss << size / (1024.0f * 1024.0f * 1024.0f) << " GB";
	}

	return oss.str();
}
}  // namespace Vulkan
}  // namespace Luna
