#pragma once

#include <glslang/SPIRV/GlslangToSpv.h>

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
class ShaderCompiler {
 public:
	ShaderCompiler();
	~ShaderCompiler() noexcept;

	std::optional<std::vector<uint32_t>> Compile(vk::ShaderStageFlagBits stage, const std::string& glsl) const;
};
}  // namespace Vulkan
}  // namespace Luna
