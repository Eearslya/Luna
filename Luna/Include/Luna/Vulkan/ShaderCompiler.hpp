#pragma once

#include <Luna/Vulkan/Common.hpp>
#include <optional>
#include <unordered_map>

namespace Luna {
namespace Vulkan {
class ShaderCompiler {
 public:
	ShaderCompiler();
	ShaderCompiler(const ShaderCompiler&)            = delete;
	ShaderCompiler(ShaderCompiler&&)                 = delete;
	ShaderCompiler& operator=(const ShaderCompiler&) = delete;
	ShaderCompiler& operator=(ShaderCompiler&&)      = delete;
	~ShaderCompiler() noexcept;

	std::optional<std::vector<uint32_t>> Compile(vk::ShaderStageFlagBits stage,
	                                             const std::string& glsl,
	                                             const std::unordered_map<std::string, int>& defines = {}) const;
};
}  // namespace Vulkan
}  // namespace Luna
